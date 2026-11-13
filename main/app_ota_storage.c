#include "app_ota_storage.h"
#include "app_config_store.h"
#include "app_timezones.h"
#include <string.h>
#include "nvs.h"

/* The existing encrypted settings namespace is also cleared by factory reset. */
#define OTA_NAMESPACE app_settings_namespace()
#define OTA_SETTINGS_KEY "ota_auto"
#define OTA_HISTORY_KEY "ota_history"

static esp_err_t read_blob(nvs_handle_t handle, const char *name, void *value, size_t expected)
{
    size_t size = expected;
    esp_err_t err = nvs_get_blob(handle, name, value, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    return err == ESP_OK && size != expected ? ESP_ERR_INVALID_SIZE : err;
}

static bool valid_history(const app_ota_history_t *history)
{
    uint32_t parts[3];
    const char *versions[] = {history->pending_version, history->blocked_version, history->installed_version};
    if (history->schema != 1 || history->result > APP_OTA_RESULT_ROLLED_BACK ||
        (history->checked_at != 0 && !app_timezone_time_supported(history->checked_at)) ||
        (history->installed_at != 0 && !app_timezone_time_supported(history->installed_at))) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (memchr(versions[i], '\0', APP_OTA_VERSION_LENGTH) == NULL ||
            (versions[i][0] && !app_ota_parse_version(versions[i], parts))) return false;
    }
    return true;
}

esp_err_t app_ota_storage_load(app_ota_settings_t *settings, app_ota_history_t *history)
{
    app_ota_settings_defaults(settings);
    memset(history, 0, sizeof(*history));
    history->schema = 1;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    app_ota_settings_t stored_settings = *settings;
    app_ota_history_t stored_history = *history;
    err = read_blob(handle, OTA_SETTINGS_KEY, &stored_settings, sizeof(stored_settings));
    if (err == ESP_OK) err = read_blob(handle, OTA_HISTORY_KEY, &stored_history, sizeof(stored_history));
    nvs_close(handle);
    if (err != ESP_OK) return err;
    if (!app_ota_settings_valid(&stored_settings) || !valid_history(&stored_history)) return ESP_ERR_INVALID_STATE;
    *settings = stored_settings;
    *history = stored_history;
    return ESP_OK;
}

esp_err_t app_ota_storage_save_settings(const app_ota_settings_t *settings, const app_ota_history_t *history)
{
    if (!app_ota_settings_valid(settings) || !valid_history(history)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, OTA_HISTORY_KEY, history, sizeof(*history));
    if (err == ESP_OK) err = nvs_set_blob(handle, OTA_SETTINGS_KEY, settings, sizeof(*settings));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_ota_storage_save_history(const app_ota_history_t *history)
{
    if (!valid_history(history)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, OTA_HISTORY_KEY, history, sizeof(*history));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
