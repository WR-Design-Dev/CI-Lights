#include "app_backup.h"
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#define BACKUP_ITERATIONS 600000
static const unsigned char AAD[] = "CI-Lights encrypted configuration v1";

bool app_backup_password_valid(const char *password)
{
    if (!password) return false;
    size_t size = strlen(password);
    if (size < APP_BACKUP_PASSWORD_MIN || size > APP_BACKUP_PASSWORD_MAX) return false;
    for (size_t i = 0; i < size; ++i) if ((unsigned char) password[i] < 32 || (unsigned char) password[i] == 127) return false;
    return true;
}

static void wipe_json(cJSON *json)
{
    if (json->valuestring && !(json->type & cJSON_IsReference))
        mbedtls_platform_zeroize(json->valuestring, strlen(json->valuestring));
    for (cJSON *child = json->child; child; child = child->next) wipe_json(child);
}

void app_backup_json_delete(cJSON *json) { if (json) { wipe_json(json); cJSON_Delete(json); } }

cJSON *app_backup_json_parse(const char *text)
{
    if (!text) return NULL;
    unsigned depth = 0;
    bool quoted = false;
    for (const char *p = text; *p; ++p) {
        if (quoted) {
            if (*p == '\\') {
                if (!strncmp(p, "\\u0000", 6) || !p[1]) return NULL;
                ++p;
            } else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
        else if (*p == '{' || *p == '[') { if (++depth > 12) return NULL; }
        else if (*p == '}' || *p == ']') { if (!depth) return NULL; --depth; }
    }
    return depth || quoted ? NULL : cJSON_ParseWithOpts(text, NULL, true);
}

char *app_backup_base64(const void *data, size_t size)
{
    size_t capacity = 4 * ((size + 2) / 3) + 1, written;
    char *text = malloc(capacity);
    if (!text) return NULL;
    if (mbedtls_base64_encode((unsigned char *) text, capacity, &written, data, size)) { free(text); return NULL; }
    text[written] = 0;
    return text;
}

esp_err_t app_backup_unbase64(const cJSON *value, void *data, size_t capacity, size_t *size)
{
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) > 4 * ((capacity + 2) / 3))
        return ESP_ERR_INVALID_ARG;
    return mbedtls_base64_decode(data, capacity, size, (const unsigned char *) value->valuestring,
                                strlen(value->valuestring)) == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

typedef struct {
    const char *password;
    const unsigned char *salt, *nonce, *input;
    unsigned char *output;
    size_t input_size, capacity, output_size;
    bool decrypt;
    SemaphoreHandle_t finished;
    esp_err_t result;
} crypto_job_t;

/* PBKDF2 runs at idle priority so the watchdog and networking keep running. */
static void crypto_task(void *argument)
{
    crypto_job_t *job = argument;
    psa_key_derivation_operation_t derivation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    unsigned char key_bytes[32] = {0};
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) status = psa_key_derivation_setup(&derivation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_integer(&derivation, PSA_KEY_DERIVATION_INPUT_COST, BACKUP_ITERATIONS);
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_bytes(&derivation, PSA_KEY_DERIVATION_INPUT_SALT, job->salt, 16);
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_bytes(&derivation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                        (const unsigned char *) job->password, strlen(job->password));
    if (status == PSA_SUCCESS) status = psa_key_derivation_output_bytes(&derivation, key_bytes, sizeof(key_bytes));
    psa_key_derivation_abort(&derivation);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    if (status == PSA_SUCCESS) status = psa_import_key(&attributes, key_bytes, sizeof(key_bytes), &key);
    mbedtls_platform_zeroize(key_bytes, sizeof(key_bytes));
    if (status == PSA_SUCCESS) {
        if (job->decrypt) status = psa_aead_decrypt(key, PSA_ALG_GCM, job->nonce, 12, AAD, sizeof(AAD) - 1,
                         job->input, job->input_size, job->output, job->capacity, &job->output_size);
        else status = psa_aead_encrypt(key, PSA_ALG_GCM, job->nonce, 12, AAD, sizeof(AAD) - 1,
                         job->input, job->input_size, job->output, job->capacity, &job->output_size);
    }
    psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    job->result = status == PSA_SUCCESS ? ESP_OK : status == PSA_ERROR_INVALID_SIGNATURE ? ESP_ERR_INVALID_CRC : ESP_FAIL;
    xSemaphoreGive(job->finished);
    vTaskDelete(NULL);
}

static esp_err_t run_crypto(crypto_job_t *job)
{
    job->finished = xSemaphoreCreateBinary();
    if (!job->finished) return ESP_ERR_NO_MEM;
    if (xTaskCreate(crypto_task, "config_crypto", 6144, job, tskIDLE_PRIORITY, NULL) != pdPASS) {
        vSemaphoreDelete(job->finished); return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(job->finished, portMAX_DELAY);
    vSemaphoreDelete(job->finished);
    return job->result;
}

esp_err_t app_backup_seal(const char *password, const char *plain, cJSON **sealed)
{
    *sealed = NULL;
    if (!app_backup_password_valid(password) || !plain || strlen(plain) > APP_BACKUP_MAX_PLAIN_BYTES) return ESP_ERR_INVALID_ARG;
    unsigned char salt[16], nonce[12];
    esp_fill_random(salt, sizeof(salt)); esp_fill_random(nonce, sizeof(nonce));
    size_t length = strlen(plain);
    unsigned char *cipher = malloc(length + 16);
    if (!cipher) return ESP_ERR_NO_MEM;
    crypto_job_t job = {.password = password, .salt = salt, .nonce = nonce, .input = (const unsigned char *) plain,
        .input_size = length, .output = cipher, .capacity = length + 16};
    esp_err_t err = run_crypto(&job);
    char *salt_text = NULL, *nonce_text = NULL, *data_text = NULL;
    cJSON *json = NULL;
    if (err == ESP_OK) {
        salt_text = app_backup_base64(salt, sizeof(salt)); nonce_text = app_backup_base64(nonce, sizeof(nonce));
        data_text = app_backup_base64(cipher, job.output_size); json = cJSON_CreateObject();
        if (!salt_text || !nonce_text || !data_text || !json ||
            !cJSON_AddStringToObject(json, "format", "ci-lights-backup") || !cJSON_AddNumberToObject(json, "schema", 1) ||
            !cJSON_AddStringToObject(json, "kdf", "PBKDF2-SHA256") || !cJSON_AddNumberToObject(json, "iterations", BACKUP_ITERATIONS) ||
            !cJSON_AddStringToObject(json, "cipher", "AES-256-GCM") || !cJSON_AddStringToObject(json, "salt", salt_text) ||
            !cJSON_AddStringToObject(json, "nonce", nonce_text) || !cJSON_AddStringToObject(json, "data", data_text)) err = ESP_ERR_NO_MEM;
    }
    free(salt_text); free(nonce_text); free(data_text); free(cipher);
    if (err == ESP_OK) *sealed = json; else cJSON_Delete(json);
    return err;
}

static bool equal_string(const cJSON *json, const char *key, const char *expected)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(value) && !strcmp(value->valuestring, expected);
}

esp_err_t app_backup_open(const char *password, const cJSON *sealed, char **plain)
{
    *plain = NULL;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(sealed, "schema");
    const cJSON *iterations = cJSON_GetObjectItemCaseSensitive(sealed, "iterations");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(sealed, "data");
    if (!app_backup_password_valid(password) || !cJSON_IsObject(sealed) || !cJSON_IsNumber(schema) || schema->valuedouble != 1 ||
        !cJSON_IsNumber(iterations) || iterations->valuedouble != BACKUP_ITERATIONS ||
        !equal_string(sealed, "format", "ci-lights-backup") || !equal_string(sealed, "kdf", "PBKDF2-SHA256") ||
        !equal_string(sealed, "cipher", "AES-256-GCM") || !cJSON_IsString(data)) return ESP_ERR_INVALID_ARG;
    unsigned char salt[16], nonce[12];
    size_t size;
    if (app_backup_unbase64(cJSON_GetObjectItemCaseSensitive(sealed, "salt"), salt, sizeof(salt), &size) || size != sizeof(salt) ||
        app_backup_unbase64(cJSON_GetObjectItemCaseSensitive(sealed, "nonce"), nonce, sizeof(nonce), &size) || size != sizeof(nonce)) return ESP_ERR_INVALID_ARG;
    size_t capacity = APP_BACKUP_MAX_PLAIN_BYTES + 16;
    unsigned char *cipher = malloc(capacity), *output = calloc(1, APP_BACKUP_MAX_PLAIN_BYTES + 1);
    if (!cipher || !output) { free(cipher); free(output); return ESP_ERR_NO_MEM; }
    esp_err_t err = app_backup_unbase64(data, cipher, capacity, &size);
    if (err == ESP_OK && size < 17) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        crypto_job_t job = {.password = password, .salt = salt, .nonce = nonce, .input = cipher, .input_size = size,
            .output = output, .capacity = APP_BACKUP_MAX_PLAIN_BYTES, .decrypt = true};
        err = run_crypto(&job);
        if (err == ESP_OK && memchr(output, 0, job.output_size)) err = ESP_ERR_INVALID_ARG;
    }
    free(cipher);
    if (err == ESP_OK) *plain = (char *) output;
    else { mbedtls_platform_zeroize(output, APP_BACKUP_MAX_PLAIN_BYTES + 1); free(output); }
    return err;
}
