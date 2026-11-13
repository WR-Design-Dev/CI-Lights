#include "app_backup.h"
#include "app_branding.h"
#include "app_config_store.h"
#include "app_ota_storage.h"
#include "traffic_light.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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

static portMUX_TYPE s_prepared_lock = portMUX_INITIALIZER_UNLOCKED;
static imported_t *s_prepared;
static char s_import_id[65];
static int64_t s_expires_at;
static esp_timer_handle_t s_expiry_timer;

static void free_import(imported_t *imported)
{
    if (!imported) return;
    for (unsigned i = 0; i < 2; ++i) free(imported->assets[i]);
    mbedtls_platform_zeroize(imported, sizeof(*imported));
    free(imported);
}

static imported_t *take_prepared_locked(void)
{
    imported_t *imported = s_prepared;
    s_prepared = NULL;
    mbedtls_platform_zeroize(s_import_id, sizeof(s_import_id));
    s_expires_at = 0;
    return imported;
}

void app_backup_discard_prepared(void)
{
    portENTER_CRITICAL(&s_prepared_lock);
    imported_t *imported = take_prepared_locked();
    portEXIT_CRITICAL(&s_prepared_lock);
    free_import(imported);
}

static void expire_prepared(void *argument)
{
    (void) argument;
    int64_t now = esp_timer_get_time();
    imported_t *expired = NULL;
    portENTER_CRITICAL(&s_prepared_lock);
    if (s_prepared && now >= s_expires_at) expired = take_prepared_locked();
    portEXIT_CRITICAL(&s_prepared_lock);
    free_import(expired);
}

static esp_err_t prepare_expiry_timer(void)
{
    if (s_expiry_timer) return ESP_OK;
    const esp_timer_create_args_t args = {.callback = expire_prepared, .name = "backup_expiry"};
    esp_err_t err = esp_timer_create(&args, &s_expiry_timer);
    if (err != ESP_OK) return err;
    err = esp_timer_start_periodic(s_expiry_timer, 1000000);
    if (err != ESP_OK) { esp_timer_delete(s_expiry_timer); s_expiry_timer = NULL; }
    return err;
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
        if (err == ESP_OK) {
            unsigned char *compact = realloc(imported->assets[i], imported->sizes[i]);
            if (compact) imported->assets[i] = compact;
        }
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
    app_backup_discard_prepared();
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
    if (err == ESP_OK) err = prepare_expiry_timer();
    char import_id[sizeof(s_import_id)];
    if (err == ESP_OK) {
        unsigned char random[32];
        static const char HEX[] = "0123456789abcdef";
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < sizeof(random); ++i) {
            import_id[i * 2] = HEX[random[i] >> 4];
            import_id[i * 2 + 1] = HEX[random[i] & 15];
        }
        import_id[64] = 0;
        mbedtls_platform_zeroize(random, sizeof(random));
        if (!cJSON_AddStringToObject(json, "import_id", import_id) ||
            !cJSON_AddNumberToObject(json, "expires_in_seconds", APP_BACKUP_PREPARED_SECONDS)) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        int64_t expires_at = esp_timer_get_time() + (int64_t) APP_BACKUP_PREPARED_SECONDS * 1000000;
        portENTER_CRITICAL(&s_prepared_lock);
        s_prepared = imported;
        memcpy(s_import_id, import_id, sizeof(s_import_id));
        s_expires_at = expires_at;
        portEXIT_CRITICAL(&s_prepared_lock);
    } else free_import(imported);
    mbedtls_platform_zeroize(import_id, sizeof(import_id));
    if (err == ESP_OK) *summary = json; else cJSON_Delete(json);
    return err;
}

static esp_err_t apply_import(imported_t *imported)
{
    /* Retain this device's rollback quarantine, never the source device's history. */
    app_ota_settings_t previous;
    esp_err_t err = app_ota_storage_load(&previous, &imported->settings.ota_history);
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

esp_err_t app_backup_import_prepared(const char *import_id)
{
    if (!import_id || strlen(import_id) != sizeof(s_import_id) - 1) return ESP_ERR_NOT_FOUND;
    int64_t now = esp_timer_get_time();
    imported_t *imported = NULL, *expired = NULL;
    portENTER_CRITICAL(&s_prepared_lock);
    if (s_prepared && now >= s_expires_at) expired = take_prepared_locked();
    if (s_prepared && !memcmp(import_id, s_import_id, sizeof(s_import_id))) imported = take_prepared_locked();
    portEXIT_CRITICAL(&s_prepared_lock);
    free_import(expired);
    return imported ? apply_import(imported) : ESP_ERR_NOT_FOUND;
}

esp_err_t app_backup_import(const char *password, const cJSON *sealed)
{
    app_backup_discard_prepared();
    imported_t *imported;
    esp_err_t err = decode_import(password, sealed, &imported);
    return err == ESP_OK ? apply_import(imported) : err;
}
