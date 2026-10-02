#include "app_ota.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "sdkconfig.h"

#if !CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT || !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "CI-Lights OTA requires signed updates and boot rollback"
#endif

#define OTA_SLOT_SIZE 0x1C0000U
#define OTA_MANIFEST_LIMIT 4096U
#define OTA_HTTP_URL_LIMIT 4096U
#define OTA_URL_LIMIT 512U
#define OTA_VERSION_LIMIT 32U
#define OTA_LAYOUT "4mb-ota-v1"
#define OTA_REPOSITORY CONFIG_APP_OTA_GITHUB_REPOSITORY
#define OTA_MANIFEST_URL "https://github.com/" OTA_REPOSITORY "/releases/latest/download/ota-manifest.json"

typedef struct {
    char version[OTA_VERSION_LIMIT];
    char url[OTA_URL_LIMIT];
    uint32_t size;
    uint8_t sha256[32];
} ota_manifest_t;

typedef struct {
    const char *phase;
    const char *message;
    char latest_version[OTA_VERSION_LIMIT];
    uint32_t bytes;
    uint32_t total;
    bool busy;
    bool boot_pending;
    bool configured;
} ota_status_t;

typedef struct {
    esp_http_client_handle_t client;
    char location[OTA_HTTP_URL_LIMIT];
    bool bad_header;
} ota_http_stream_t;

static const char *TAG = "ota";
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t s_status = {.phase = "idle", .message = "Noch nicht nach Updates gesucht."};
static char s_requested_version[OTA_VERSION_LIMIT];
static bool s_boot_validation_started;

static void set_phase(const char *phase, const char *message)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.phase = phase;
    s_status.message = message;
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_error(esp_err_t err, const char *message)
{
    ESP_LOGW(TAG, "%s (%s)", message, esp_err_to_name(err));
    set_phase("error", message);
}

static bool parse_version(const char *text, uint32_t parts[3])
{
    if (text == NULL || *text == '\0' || strlen(text) >= OTA_VERSION_LIMIT) {
        return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            unsigned digit = (unsigned) (*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10) {
                return false;
            }
            value = value * 10 + digit;
        }
        parts[i] = value;
        if (i < 2 && *text++ != '.') {
            return false;
        }
    }
    return *text == '\0';
}

static bool version_is_newer(const char *candidate, const char *current)
{
    uint32_t next[3], installed[3];
    if (!parse_version(candidate, next) || !parse_version(current, installed)) {
        return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (next[i] != installed[i]) {
            return next[i] > installed[i];
        }
    }
    return false;
}

static const char *json_string(const cJSON *json, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static esp_err_t parse_manifest(const char *body, ota_manifest_t *manifest)
{
    cJSON *json = cJSON_ParseWithOpts(body, NULL, true);
    const char *version = json_string(json, "version");
    const char *project = json_string(json, "project");
    const char *chip = json_string(json, "chip");
    const char *layout = json_string(json, "layout");
    const char *url = json_string(json, "url");
    const char *sha256 = json_string(json, "sha256");
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(json, "size");
    uint32_t version_parts[3];
    char expected_url[OTA_URL_LIMIT];
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (!cJSON_IsObject(json) || !cJSON_IsNumber(schema) || schema->valuedouble != 1 ||
        !parse_version(version, version_parts) || project == NULL ||
        strcmp(project, "CI-Lights") != 0 || chip == NULL || strcmp(chip, "esp32s3") != 0 ||
        layout == NULL || strcmp(layout, OTA_LAYOUT) != 0 || url == NULL ||
        strlen(url) >= sizeof(manifest->url) || sha256 == NULL || strlen(sha256) != 64 ||
        !cJSON_IsNumber(size) || !(size->valuedouble >= 4096 && size->valuedouble <= OTA_SLOT_SIZE) ||
        size->valuedouble != (uint32_t) size->valuedouble) {
        goto done;
    }
    int length = snprintf(expected_url, sizeof(expected_url),
                          "https://github.com/" OTA_REPOSITORY "/releases/download/v%s/CI-Lights.bin",
                          version);
    if (length < 0 || length >= sizeof(expected_url) || strcmp(url, expected_url) != 0) {
        goto done;
    }
    for (unsigned i = 0; i < sizeof(manifest->sha256); ++i) {
        int high = hex_digit(sha256[i * 2]), low = hex_digit(sha256[i * 2 + 1]);
        if (high < 0 || low < 0) goto done;
        manifest->sha256[i] = (uint8_t) ((high << 4) | low);
    }
    strcpy(manifest->version, version);
    strcpy(manifest->url, url);
    manifest->size = (uint32_t) size->valuedouble;
    err = ESP_OK;
done:
    cJSON_Delete(json);
    return err;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        ota_http_stream_t *stream = event->user_data;
        if (strcasecmp(event->header_key, "Location") == 0) {
            size_t length = strlen(event->header_value);
            if (length >= sizeof(stream->location)) {
                stream->bad_header = true;
            } else {
                memcpy(stream->location, event->header_value, length + 1);
            }
        } else if (strcasecmp(event->header_key, "Content-Encoding") == 0 &&
                   strcasecmp(event->header_value, "identity") != 0) {
            stream->bad_header = true;
        }
    }
    return ESP_OK;
}

static void close_stream(ota_http_stream_t *stream)
{
    if (stream->client != NULL) {
        esp_http_client_close(stream->client);
        esp_http_client_cleanup(stream->client);
        stream->client = NULL;
    }
}

/* Handle redirects explicitly so that every hop must remain HTTPS. */
static esp_err_t open_stream(const char *url, ota_http_stream_t *stream, int64_t *length)
{
    char *next = malloc(OTA_HTTP_URL_LIMIT);
    if (next == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (strlen(url) >= OTA_HTTP_URL_LIMIT) goto done;
    strcpy(next, url);
    for (unsigned redirect = 0; redirect < 6; ++redirect) {
        if (strncmp(next, "https://", 8) != 0) {
            err = ESP_ERR_INVALID_ARG;
            break;
        }
        stream->location[0] = '\0';
        stream->bad_header = false;
        esp_http_client_config_t config = {
            .url = next,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
            .buffer_size = 4096,
            .buffer_size_tx = 4096,
            .disable_auto_redirect = true,
            .event_handler = http_event,
            .user_data = stream,
            .user_agent = "CI-Lights-OTA/1",
        };
        stream->client = esp_http_client_init(&config);
        if (stream->client == NULL) { err = ESP_ERR_NO_MEM; break; }
        esp_http_client_set_header(stream->client, "Accept-Encoding", "identity");
        err = esp_http_client_open(stream->client, 0);
        if (err != ESP_OK) break;
        *length = esp_http_client_fetch_headers(stream->client);
        int status = esp_http_client_get_status_code(stream->client);
        if (stream->bad_header || (*length < 0 && !esp_http_client_is_chunked_response(stream->client))) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (status == 200) { err = ESP_OK; break; }
        if (status == 404) { err = ESP_ERR_NOT_FOUND; break; }
        if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
            stream->location[0] == '\0') {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (stream->location[0] == '/' && stream->location[1] != '/') {
            char *path = strchr(next + 8, '/');
            size_t origin = path == NULL ? strlen(next) : (size_t) (path - next);
            size_t location_length = strlen(stream->location);
            if (origin + location_length >= OTA_HTTP_URL_LIMIT) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            memcpy(next + origin, stream->location, location_length + 1);
        } else {
            strcpy(next, stream->location);
        }
        close_stream(stream);
        err = ESP_ERR_INVALID_RESPONSE;
    }
done:
    free(next);
    if (err != ESP_OK) close_stream(stream);
    return err;
}

static esp_err_t fetch_manifest(ota_manifest_t *manifest)
{
    char *body = calloc(1, OTA_MANIFEST_LIMIT + 1);
    ota_http_stream_t *stream = calloc(1, sizeof(*stream));
    if (body == NULL || stream == NULL) { free(body); free(stream); return ESP_ERR_NO_MEM; }
    int64_t content_length = 0;
    esp_err_t err = open_stream(OTA_MANIFEST_URL, stream, &content_length);
    if (err != ESP_OK) goto done;
    if (content_length > OTA_MANIFEST_LIMIT) { err = ESP_ERR_INVALID_SIZE; goto done; }
    size_t received = 0;
    while (received < OTA_MANIFEST_LIMIT) {
        int count = esp_http_client_read(stream->client, body + received, OTA_MANIFEST_LIMIT - received);
        if (count < 0) { err = ESP_FAIL; goto done; }
        if (count == 0) break;
        received += (size_t) count;
    }
    if (!esp_http_client_is_complete_data_received(stream->client) || received == 0 ||
        memchr(body, '\0', received) != NULL) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    body[received] = '\0';
    err = parse_manifest(body, manifest);
done:
    close_stream(stream);
    free(stream);
    free(body);
    return err;
}

static esp_err_t download_image(const ota_manifest_t *manifest)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->size > partition->size) return ESP_ERR_INVALID_SIZE;
    ota_http_stream_t *stream = calloc(1, sizeof(*stream));
    uint8_t *buffer = heap_caps_malloc_prefer(4096, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stream == NULL || buffer == NULL) { free(stream); free(buffer); return ESP_ERR_NO_MEM; }
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    mbedtls_md_context_t hash;
    mbedtls_md_init(&hash);
    int64_t content_length = 0;
    esp_err_t err = open_stream(manifest->url, stream, &content_length);
    if (err != ESP_OK) goto done;
    if (content_length > 0 && content_length != manifest->size) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    if (mbedtls_md_setup(&hash, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
        mbedtls_md_starts(&hash) != 0) { err = ESP_FAIL; goto done; }
    uint8_t first[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
    size_t first_received = 0;
    while (first_received < sizeof(first)) {
        int count = esp_http_client_read(stream->client, (char *) first + first_received,
                                         sizeof(first) - first_received);
        if (count <= 0) { err = ESP_ERR_INVALID_RESPONSE; goto done; }
        first_received += (size_t) count;
    }
    esp_image_header_t header;
    memcpy(&header, first, sizeof(header));
    esp_app_desc_t description;
    memcpy(&description, first + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
           sizeof(description));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
        description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        memchr(description.version, '\0', sizeof(description.version)) == NULL ||
        memchr(description.project_name, '\0', sizeof(description.project_name)) == NULL ||
        strcmp(description.version, manifest->version) != 0 ||
        strcmp(description.project_name, "CI-Lights") != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    err = esp_ota_begin(partition, manifest->size, &ota);
    if (err != ESP_OK) goto done;
    ota_open = true;
    err = esp_ota_write(ota, first, first_received);
    if (err != ESP_OK || mbedtls_md_update(&hash, first, first_received) != 0) {
        if (err == ESP_OK) err = ESP_FAIL;
        goto done;
    }
    uint32_t received = first_received;
    while (true) {
        int count = esp_http_client_read(stream->client, (char *) buffer, 4096);
        if (count < 0) { err = ESP_FAIL; goto done; }
        if (count == 0) break;
        if ((uint32_t) count > manifest->size - received) { err = ESP_ERR_INVALID_SIZE; goto done; }
        err = esp_ota_write(ota, buffer, count);
        if (err != ESP_OK || mbedtls_md_update(&hash, buffer, count) != 0) {
            if (err == ESP_OK) err = ESP_FAIL;
            goto done;
        }
        received += (uint32_t) count;
        portENTER_CRITICAL(&s_status_lock);
        s_status.bytes = received;
        portEXIT_CRITICAL(&s_status_lock);
        taskYIELD();
    }
    if (received != manifest->size || !esp_http_client_is_complete_data_received(stream->client)) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    uint8_t digest[32];
    if (mbedtls_md_finish(&hash, digest) != 0 || memcmp(digest, manifest->sha256, sizeof(digest)) != 0) {
        err = ESP_ERR_OTA_VALIDATE_FAILED;
        goto done;
    }
    close_stream(stream);
    set_phase("verifying", "Firmware-Signatur wird geprüft.");
    /* esp_ota_end verifies the image and its RSA signature before changing boot selection. */
    err = esp_ota_end(ota);
    ota_open = false;
    if (err == ESP_OK) err = esp_ota_set_boot_partition(partition);
done:
    if (ota_open) esp_ota_abort(ota);
    close_stream(stream);
    mbedtls_md_free(&hash);
    free(stream);
    heap_caps_free(buffer);
    return err;
}

static void update_task(void *argument)
{
    bool install = (uintptr_t) argument != 0;
    ota_manifest_t manifest = {0};
    esp_err_t err = fetch_manifest(&manifest);
    if (err != ESP_OK) {
        set_error(err, err == ESP_ERR_NOT_FOUND ? "Noch kein öffentliches Firmware-Release gefunden." :
                  "Update-Prüfung fehlgeschlagen. Internet und Release-Dateien prüfen.");
        goto done;
    }
    portENTER_CRITICAL(&s_status_lock);
    strcpy(s_status.latest_version, manifest.version);
    s_status.total = manifest.size;
    portEXIT_CRITICAL(&s_status_lock);
    if (!version_is_newer(manifest.version, esp_app_get_description()->version)) {
        set_phase("up_to_date", "Die Firmware ist aktuell.");
        goto done;
    }
    if (!install) { set_phase("available", "Neue Firmware ist verfügbar."); goto done; }
    if (strcmp(manifest.version, s_requested_version) != 0) {
        set_error(ESP_ERR_INVALID_STATE, "Das Release hat sich geändert. Bitte erneut nach Updates suchen.");
        goto done;
    }
    set_phase("downloading", "Firmware wird heruntergeladen. Stromversorgung angeschlossen lassen.");
    err = download_image(&manifest);
    if (err != ESP_OK) {
        set_error(err, "Update fehlgeschlagen. Die bisherige Firmware bleibt aktiv.");
        goto done;
    }
    set_phase("rebooting", "Update geprüft. Die Ampel startet neu.");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
done:
    portENTER_CRITICAL(&s_status_lock);
    s_status.busy = false;
    portEXIT_CRITICAL(&s_status_lock);
    vTaskDelete(NULL);
}

void app_ota_init(void)
{
    uint32_t physical_size = 0;
    const esp_partition_t *ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    s_status.configured = esp_flash_get_physical_size(NULL, &physical_size) == ESP_OK && physical_size >= 0x400000 &&
                          ota0 != NULL && ota1 != NULL && data != NULL &&
                          ota0->address == 0x20000 && ota1->address == 0x200000 &&
                          ota0->size == OTA_SLOT_SIZE && ota1->size == OTA_SLOT_SIZE && data->size == 0x2000;
    esp_ota_img_states_t state;
    s_status.boot_pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                            state == ESP_OTA_IMG_PENDING_VERIFY;
    if (!s_status.configured) set_phase("disabled", "OTA-Partitionierung fehlt. Einmal vollständig per USB flashen.");
    else if (s_status.boot_pending) set_phase("boot_validation", "Neue Firmware wird beim Start geprüft.");
}

static void boot_validation_task(void *argument)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Startprüfung konnte nicht bestätigt werden: %s", esp_err_to_name(err));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    } else {
        portENTER_CRITICAL(&s_status_lock);
        s_status.boot_pending = false;
        s_status.phase = "idle";
        s_status.message = "Startprüfung erfolgreich. Firmware bestätigt.";
        portEXIT_CRITICAL(&s_status_lock);
    }
    vTaskDelete(NULL);
}

void app_ota_confirm_startup(bool web_server_ready)
{
    if (!s_status.boot_pending || s_boot_validation_started) return;
    if (!web_server_ready || !s_status.configured ||
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) < 1024 * 1024 ||
        xTaskCreate(boot_validation_task, "ota_boot_check", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Startprüfung fehlgeschlagen; kehre zur vorherigen Firmware zurück");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }
    s_boot_validation_started = true;
}

esp_err_t app_ota_status_handler(httpd_req_t *request)
{
    ota_status_t status;
    portENTER_CRITICAL(&s_status_lock);
    status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "phase", status.phase) == NULL ||
        cJSON_AddStringToObject(json, "message", status.message) == NULL ||
        cJSON_AddStringToObject(json, "current_version", esp_app_get_description()->version) == NULL ||
        cJSON_AddStringToObject(json, "latest_version", status.latest_version) == NULL ||
        cJSON_AddStringToObject(json, "repository", OTA_REPOSITORY) == NULL ||
        cJSON_AddNumberToObject(json, "bytes", status.bytes) == NULL ||
        cJSON_AddNumberToObject(json, "total", status.total) == NULL ||
        cJSON_AddBoolToObject(json, "busy", status.busy) == NULL ||
        cJSON_AddBoolToObject(json, "boot_pending", status.boot_pending) == NULL ||
        cJSON_AddBoolToObject(json, "configured", status.configured) == NULL) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Nicht genug Speicher.");
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, body);
    free(body);
    return err;
}

static esp_err_t start_update(httpd_req_t *request, bool install)
{
    char content_type[48];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0 || request->content_len <= 0 || request->content_len >= 128) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "JSON-Anfrage erforderlich.");
    }
    char body[128] = {0};
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Anfrage unvollständig.");
        received += count;
    }
    cJSON *json = memchr(body, '\0', received) == NULL ? cJSON_ParseWithOpts(body, NULL, true) : NULL;
    const char *version = json_string(json, "version");
    uint32_t parts[3];
    if (!cJSON_IsObject(json) || (install && !parse_version(version, parts))) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Ungültige Firmware-Version.");
    }
    portENTER_CRITICAL(&s_status_lock);
    bool allowed = s_status.configured && !s_status.busy && !s_status.boot_pending &&
                   (!install || (strcmp(s_status.phase, "available") == 0 &&
                                 strcmp(s_status.latest_version, version) == 0));
    if (allowed) {
        if (install) strcpy(s_requested_version, version);
        s_status.busy = true;
        s_status.phase = "checking";
        s_status.message = "GitHub-Release wird geprüft.";
        s_status.bytes = 0;
    }
    portEXIT_CRITICAL(&s_status_lock);
    cJSON_Delete(json);
    if (!allowed) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Update beschäftigt, noch nicht geprüft oder nicht eingerichtet.");
    }
    if (xTaskCreate(update_task, "ota_update", 12288, (void *) (uintptr_t) install, 3, NULL) != pdPASS) {
        set_error(ESP_ERR_NO_MEM, "Nicht genug Speicher für das Update.");
        portENTER_CRITICAL(&s_status_lock);
        s_status.busy = false;
        portEXIT_CRITICAL(&s_status_lock);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Nicht genug Speicher.");
    }
    httpd_resp_set_status(request, "202 Accepted");
    return app_ota_status_handler(request);
}

esp_err_t app_ota_check_handler(httpd_req_t *request) { return start_update(request, false); }
esp_err_t app_ota_install_handler(httpd_req_t *request) { return start_update(request, true); }

extern const unsigned char ota_js_start[] asm("_binary_ota_js_start");
extern const unsigned char ota_js_end[] asm("_binary_ota_js_end");

esp_err_t app_ota_script_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(request, (const char *) ota_js_start, ota_js_end - ota_js_start - 1);
}
