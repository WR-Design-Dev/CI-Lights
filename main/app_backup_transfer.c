#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "app_backup_transfer.h"
#include "app_cpu.h"
#include "app_ota.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static char s_id[65];
static bool s_running, s_restart_pending;
static int64_t s_expires_at, s_restart_at;
static esp_err_t s_error;
static app_backup_transfer_kind_t s_kind;
static cJSON *s_request, *s_result;

static void clear_result(void)
{
    app_backup_json_delete(s_result);
    s_result = NULL;
    memset(s_id, 0, sizeof(s_id));
    s_expires_at = 0;
}

static void maintenance(void *argument)
{
    (void) argument;
    bool restart = false;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_restart_pending && s_restart_at && now >= s_restart_at) {
        s_restart_at = 0;
        restart = true;
    }
    if (!s_running && !s_restart_pending && s_expires_at && now >= s_expires_at) clear_result();
    xSemaphoreGive(s_lock);
    if (restart) esp_restart();
}

esp_err_t app_backup_transfer_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_running) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    clear_result();
    s_restart_pending = false;
    s_restart_at = 0;
    xSemaphoreGive(s_lock);
    if (s_timer) return ESP_OK;
    const esp_timer_create_args_t args = {.callback = maintenance, .name = "config_transfer"};
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) return err;
    err = esp_timer_start_periodic(s_timer, 1000000);
    if (err != ESP_OK) { esp_timer_delete(s_timer); s_timer = NULL; }
    return err;
}

static const char *string_field(const cJSON *json, const char *key)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(field) ? field->valuestring : "";
}

static const char *error_message(void)
{
    if (s_kind == APP_BACKUP_TRANSFER_PREPARED && s_error == ESP_ERR_NOT_FOUND)
        return "Die geprüfte Konfiguration ist abgelaufen oder nicht mehr verfügbar. Datei erneut prüfen oder direkt übernehmen.";
    if (s_error == ESP_ERR_INVALID_CRC) return "Sicherungspasswort falsch oder Datei beschädigt.";
    if (s_error == ESP_ERR_INVALID_ARG || s_error == ESP_ERR_INVALID_SIZE)
        return "Konfigurationsdatei ungültig oder nicht mit dieser Firmware kompatibel.";
    return "Konfiguration konnte nicht übertragen werden. Speicher und Verbindung prüfen.";
}

static void transfer_task(void *argument)
{
    (void) argument;
    bool boosted = app_cpu_boost_begin();
    int64_t started = esp_timer_get_time();
    app_backup_transfer_kind_t kind = s_kind;
    cJSON *request = s_request, *result = NULL;
    bool importing = kind == APP_BACKUP_TRANSFER_IMPORT || kind == APP_BACKUP_TRANSFER_PREPARED;
    esp_err_t err = ESP_OK;
    ESP_LOGI("provision", "Konfigurationsauftrag %u gestartet", (unsigned) kind);
    /* Allocate the acknowledgement before activating a new configuration. */
    if (importing) {
        result = cJSON_CreateObject();
        if (!result || !cJSON_AddBoolToObject(result, "restarting", true)) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        const char *password = string_field(request, "password");
        const cJSON *backup = cJSON_GetObjectItemCaseSensitive(request, "backup");
        if (kind == APP_BACKUP_TRANSFER_EXPORT) err = app_backup_export(password, &result);
        else if (kind == APP_BACKUP_TRANSFER_VALIDATE) err = app_backup_validate(password, backup, &result);
        else if (kind == APP_BACKUP_TRANSFER_PREPARED) err = app_backup_import_prepared(string_field(request, "import_id"));
        else err = app_backup_import(password, backup);
    }
    app_backup_json_delete(request);
    app_cpu_boost_end(boosted);
    if (err != ESP_OK || !importing) app_ota_end_config_transfer();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_request = NULL;
    s_result = result;
    s_error = err;
    s_expires_at = esp_timer_get_time() + (int64_t) 300 * 1000000;
    s_restart_pending = importing && err == ESP_OK;
    /* Restart even if the browser disappeared after submitting the import. */
    s_restart_at = s_restart_pending ? esp_timer_get_time() + (int64_t) 30 * 1000000 : 0;
    s_running = false;
    xSemaphoreGive(s_lock);
    ESP_LOGI("provision", "Konfigurationsauftrag %u nach %ld ms beendet: %s", (unsigned) kind,
             (long) ((esp_timer_get_time() - started) / 1000), esp_err_to_name(err));
    vTaskDelete(NULL);
}

esp_err_t app_backup_transfer_start(app_backup_transfer_kind_t kind, cJSON *request, char operation_id[65])
{
    if (!s_lock || !s_timer) return ESP_ERR_NO_MEM;
    if (!cJSON_IsObject(request) || !operation_id || (unsigned) kind > APP_BACKUP_TRANSFER_PREPARED) return ESP_ERR_INVALID_ARG;
    unsigned char random[32];
    static const char HEX[] = "0123456789abcdef";
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        operation_id[i * 2] = HEX[random[i] >> 4];
        operation_id[i * 2 + 1] = HEX[random[i] & 15];
    }
    operation_id[64] = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_running || s_restart_pending || !app_ota_begin_config_transfer()) {
        xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE;
    }
    clear_result();
    memcpy(s_id, operation_id, sizeof(s_id));
    s_kind = kind;
    s_request = request;
    s_running = true;
    if (xTaskCreate(transfer_task, "config_transfer", 20480, NULL, 3, NULL) != pdPASS) {
        s_request = NULL; s_running = false; clear_result();
        app_ota_end_config_transfer();
        xSemaphoreGive(s_lock); return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t app_backup_transfer_poll(const char *operation_id, cJSON **response)
{
    *response = NULL;
    if (!s_lock || !operation_id || strlen(operation_id) != 64) return ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_running && !s_restart_pending && s_expires_at && esp_timer_get_time() >= s_expires_at) clear_result();
    if (strcmp(operation_id, s_id)) { xSemaphoreGive(s_lock); return ESP_ERR_NOT_FOUND; }
    cJSON *json = cJSON_CreateObject();
    bool ok = json && cJSON_AddBoolToObject(json, s_running ? "pending" : "done", true);
    if (ok && !s_running) {
        if (s_error != ESP_OK) ok = cJSON_AddStringToObject(json, "error", error_message()) != NULL;
        else {
            cJSON *copy = cJSON_Duplicate(s_result, true);
            if (copy && s_kind == APP_BACKUP_TRANSFER_VALIDATE) {
                int64_t elapsed = esp_timer_get_time() - (s_expires_at - (int64_t) 300 * 1000000);
                int remaining = APP_BACKUP_PREPARED_SECONDS - (int) ((elapsed + 999999) / 1000000);
                cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(copy, "expires_in_seconds"), remaining);
            }
            ok = copy && cJSON_AddItemToObject(json, "result", copy);
            if (!ok) app_backup_json_delete(copy);
        }
    }
    xSemaphoreGive(s_lock);
    if (!ok) { app_backup_json_delete(json); return ESP_ERR_NO_MEM; }
    *response = json;
    return ESP_OK;
}

void app_backup_transfer_acknowledge(const char *operation_id)
{
    if (!s_lock || !operation_id) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_restart_pending && !strcmp(operation_id, s_id)) {
        int64_t sooner = esp_timer_get_time() + (int64_t) 2 * 1000000;
        if (s_restart_at && sooner < s_restart_at) s_restart_at = sooner;
    }
    xSemaphoreGive(s_lock);
}

bool app_backup_transfer_running(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool running = s_running;
    xSemaphoreGive(s_lock);
    return running;
}

bool app_backup_transfer_restart_pending(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_restart_pending;
    xSemaphoreGive(s_lock);
    return pending;
}
