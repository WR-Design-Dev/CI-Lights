#include "app_backup.h"
#include "app_branding.h"
#include "app_config_store.h"
#include "app_ota_storage.h"
#include "traffic_light.h"
#include "esp_app_desc.h"
#include "mbedtls/platform_util.h"
#include <stdlib.h>
#include <string.h>

static const char *const ASSETS[] = {"banner", "favicon"};
typedef struct {
    app_settings_snapshot_t settings;
    unsigned char *assets[2];
    size_t sizes[2];
    app_branding_format_t formats[2];
} imported_t;

static void free_import(imported_t *imported)
{
    if (!imported) return;
    for (unsigned i = 0; i < 2; ++i) free(imported->assets[i]);
    mbedtls_platform_zeroize(imported, sizeof(*imported));
    free(imported);
}

static bool unique_keys(const cJSON *json, unsigned depth)
{
    if (depth > 12) return false;
    for (const cJSON *child = json->child; child; child = child->next) {
        if (!unique_keys(child, depth + 1)) return false;
        if (cJSON_IsObject(json)) for (const cJSON *next = child->next; next; next = next->next)
            if (child->string && next->string && !strcmp(child->string, next->string)) return false;
    }
    return true;
}

static esp_err_t decode_import(const char *password, const cJSON *sealed, imported_t **result)
{
    *result = NULL;
    if (!sealed || !unique_keys(sealed, 0)) return ESP_ERR_INVALID_ARG;
    char *plain = NULL;
    esp_err_t err = app_backup_open(password, sealed, &plain);
    if (err != ESP_OK) return err;
    cJSON *json = app_backup_json_parse(plain);
    mbedtls_platform_zeroize(plain, strlen(plain)); free(plain);
    imported_t *imported = calloc(1, sizeof(*imported));
    if (!imported) err = ESP_ERR_NO_MEM;
    else if (!json || !unique_keys(json, 0)) err = ESP_ERR_INVALID_ARG;
    else err = app_backup_parse_snapshot(json, &imported->settings);
    const cJSON *branding = cJSON_GetObjectItemCaseSensitive(json, "branding");
    if (err == ESP_OK && !cJSON_IsObject(branding)) err = ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 2 && err == ESP_OK; ++i) {
        imported->settings.branding_slots[i] = 2;
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(branding, ASSETS[i]);
        if (cJSON_IsNull(item)) continue;
        const cJSON *format = cJSON_GetObjectItemCaseSensitive(item, "format");
        if (!cJSON_IsObject(item) || !cJSON_IsNumber(format) ||
            (format->valuedouble != APP_BRANDING_PNG && format->valuedouble != (i == 0 ? APP_BRANDING_SVG : APP_BRANDING_ICO))) {
            err = ESP_ERR_INVALID_ARG; break;
        }
        imported->formats[i] = format->valueint;
        imported->assets[i] = malloc(APP_BRANDING_MAX_BYTES);
        if (!imported->assets[i]) { err = ESP_ERR_NO_MEM; break; }
        err = app_backup_unbase64(cJSON_GetObjectItemCaseSensitive(item, "data"), imported->assets[i],
                                  APP_BRANDING_MAX_BYTES, &imported->sizes[i]);
        if (err == ESP_OK && !app_branding_data_valid(imported->formats[i], imported->assets[i], imported->sizes[i])) err = ESP_ERR_INVALID_ARG;
    }
    app_backup_json_delete(json);
    if (err == ESP_OK) *result = imported; else free_import(imported);
    return err;
}

esp_err_t app_backup_export(const char *password, cJSON **sealed)
{
    *sealed = NULL;
    app_settings_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return ESP_ERR_NO_MEM;
    esp_err_t err = app_settings_snapshot_load(snapshot);
    if (err == ESP_OK) {
        snapshot->lights.mode = traffic_light_control_mode();
        snapshot->lights.disco_effect = traffic_light_disco_effect();
        for (unsigned i = 0; i < 3; ++i) {
            traffic_light_manual_color((app_manual_light_t) i, &snapshot->lights.colors[i][0],
                                       &snapshot->lights.colors[i][1], &snapshot->lights.colors[i][2]);
            if (snapshot->lights.mode == APP_CONTROL_MODE_MANUAL) snapshot->lights.enabled[i] = traffic_light_manual_state((app_manual_light_t) i);
        }
    }
    cJSON *json = err == ESP_OK ? app_backup_snapshot_json(snapshot) : NULL;
    if (err == ESP_OK && !json) err = ESP_ERR_NO_MEM;
    cJSON *branding = json ? cJSON_AddObjectToObject(json, "branding") : NULL;
    if (err == ESP_OK && (!branding || !cJSON_AddStringToObject(json, "firmware_version", esp_app_get_description()->version))) err = ESP_ERR_NO_MEM;
    for (unsigned i = 0; i < 2 && err == ESP_OK; ++i) {
        app_branding_info_t info;
        if (!app_branding_get_info((app_branding_kind_t) i, &info)) {
            if (!cJSON_AddNullToObject(branding, ASSETS[i])) err = ESP_ERR_NO_MEM;
            continue;
        }
        unsigned char *data = malloc(info.length);
        if (!data) { err = ESP_ERR_NO_MEM; break; }
        err = app_branding_read((app_branding_kind_t) i, data, info.length);
        char *text = err == ESP_OK ? app_backup_base64(data, info.length) : NULL;
        free(data);
        cJSON *item = cJSON_AddObjectToObject(branding, ASSETS[i]);
        if (err == ESP_OK && (!text || !item || !cJSON_AddNumberToObject(item, "format", info.format) ||
            !cJSON_AddStringToObject(item, "data", text))) err = ESP_ERR_NO_MEM;
        free(text);
    }
    char *plain = err == ESP_OK ? cJSON_PrintUnformatted(json) : NULL;
    if (err == ESP_OK) err = plain ? app_backup_seal(password, plain, sealed) : ESP_ERR_NO_MEM;
    if (plain) { mbedtls_platform_zeroize(plain, strlen(plain)); free(plain); }
    app_backup_json_delete(json);
    mbedtls_platform_zeroize(snapshot, sizeof(*snapshot)); free(snapshot);
    return err;
}

esp_err_t app_backup_validate(const char *password, const cJSON *sealed, cJSON **summary)
{
    *summary = NULL;
    imported_t *imported;
    esp_err_t err = decode_import(password, sealed, &imported);
    if (err != ESP_OK) return err;
    const app_settings_snapshot_t *s = &imported->settings;
    cJSON *json = cJSON_CreateObject();
    cJSON *networks = json ? cJSON_AddArrayToObject(json, "networks") : NULL;
    if (!networks || !cJSON_AddStringToObject(json, "title", s->title) ||
        !cJSON_AddStringToObject(json, "hostname", s->hostname) ||
        !cJSON_AddStringToObject(json, "admin_username", s->admin_username) ||
        !cJSON_AddStringToObject(json, "jenkins_url", s->config.jenkins_url) ||
        !cJSON_AddStringToObject(json, "job_path", s->config.jenkins_job_path) ||
        !cJSON_AddBoolToObject(json, "banner", imported->assets[0] != NULL) ||
        !cJSON_AddBoolToObject(json, "favicon", imported->assets[1] != NULL)) err = ESP_ERR_NO_MEM;
    for (uint8_t i = 0; i < s->wifi.count && err == ESP_OK; ++i) {
        cJSON *name = cJSON_CreateString(s->wifi.entries[i].ssid);
        if (!name || !cJSON_AddItemToArray(networks, name)) { cJSON_Delete(name); err = ESP_ERR_NO_MEM; }
    }
    free_import(imported);
    if (err == ESP_OK) *summary = json; else cJSON_Delete(json);
    return err;
}

esp_err_t app_backup_import(const char *password, const cJSON *sealed)
{
    imported_t *imported;
    esp_err_t err = decode_import(password, sealed, &imported);
    if (err != ESP_OK) return err;
    /* Retain this device's rollback quarantine, never the source device's history. */
    app_ota_settings_t previous;
    err = app_ota_storage_load(&previous, &imported->settings.ota_history);
    imported->settings.ota_history.install_day = 0;
    if (err == ESP_OK) err = app_branding_pin_active();
    for (unsigned i = 0; i < 2 && err == ESP_OK; ++i) if (imported->assets[i]) {
        err = app_branding_stage((app_branding_kind_t) i, imported->formats[i], imported->assets[i], imported->sizes[i],
                                 &imported->settings.branding_slots[i]);
    }
    if (err == ESP_OK) err = app_settings_snapshot_stage(&imported->settings);
    if (err == ESP_OK) err = app_config_store_activate();
    free_import(imported);
    return err;
}
