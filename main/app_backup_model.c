#include "app_backup.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mbedtls/platform_util.h"

cJSON *app_backup_snapshot_json(const app_settings_snapshot_t *s)
{
    if (!app_settings_snapshot_valid(s)) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *jenkins = cJSON_AddObjectToObject(root, "jenkins");
    cJSON *admin = cJSON_AddObjectToObject(root, "admin");
    cJSON *lights = cJSON_AddObjectToObject(root, "lights");
    cJSON *ota = cJSON_AddObjectToObject(root, "ota");
    if (!device || !wifi || !jenkins || !admin || !lights || !ota) goto fail;
#define STR(o,k,v) do { if (!cJSON_AddStringToObject(o,k,v)) goto fail; } while (0)
#define NUM(o,k,v) do { if (!cJSON_AddNumberToObject(o,k,v)) goto fail; } while (0)
    STR(root, "format", "ci-lights-config"); NUM(root, "schema", 1);
    STR(device, "title", s->title); STR(device, "hostname", s->hostname); STR(device, "language", s->language);
    NUM(device, "cpu_mode", s->config.cpu_mode);
    NUM(wifi, "last_index", s->wifi.last_index);
    cJSON *networks = cJSON_AddArrayToObject(wifi, "networks");
    if (!networks) goto fail;
    for (uint8_t i = 0; i < s->wifi.count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry || !cJSON_AddItemToArray(networks, entry)) { cJSON_Delete(entry); goto fail; }
        STR(entry, "ssid", s->wifi.entries[i].ssid); STR(entry, "username", s->wifi.entries[i].username);
        STR(entry, "password", s->wifi.entries[i].password); NUM(entry, "auth", s->wifi.entries[i].auth);
    }
    STR(jenkins, "url", s->config.jenkins_url); STR(jenkins, "job_path", s->config.jenkins_job_path);
    STR(jenkins, "user", s->config.jenkins_user); STR(jenkins, "token", s->config.jenkins_token);
    NUM(jenkins, "poll_interval_minutes", s->config.jenkins_poll_interval_minutes);
    NUM(jenkins, "job_list_timeout_seconds", s->config.jenkins_job_list_timeout_seconds);
    NUM(jenkins, "status_timeout_seconds", s->config.jenkins_status_timeout_seconds);
    STR(admin, "username", s->admin_username);
    char *salt = app_backup_base64(s->admin_salt, sizeof(s->admin_salt));
    char *hash = app_backup_base64(s->admin_hash, sizeof(s->admin_hash));
    bool added = salt && hash && cJSON_AddStringToObject(admin, "salt", salt) && cJSON_AddStringToObject(admin, "verifier", hash);
    if (salt) { mbedtls_platform_zeroize(salt, strlen(salt)); free(salt); }
    if (hash) { mbedtls_platform_zeroize(hash, strlen(hash)); free(hash); }
    if (!added) goto fail;
    NUM(lights, "brightness", s->config.light_brightness_percent); NUM(lights, "build_effect", s->config.build_effect);
    NUM(lights, "mode", s->lights.mode); NUM(lights, "disco_effect", s->lights.disco_effect);
    cJSON *colors = cJSON_AddArrayToObject(lights, "colors"), *enabled = cJSON_AddArrayToObject(lights, "enabled");
    if (!colors || !enabled) goto fail;
    for (unsigned i = 0; i < 3; ++i) {
        char color[8];
        snprintf(color, sizeof(color), "#%02x%02x%02x", s->lights.colors[i][0], s->lights.colors[i][1], s->lights.colors[i][2]);
        cJSON *text = cJSON_CreateString(color), *on = cJSON_CreateBool(s->lights.enabled[i]);
        if (!text || !on) { cJSON_Delete(text); cJSON_Delete(on); goto fail; }
        if (!cJSON_AddItemToArray(colors, text)) { cJSON_Delete(text); cJSON_Delete(on); goto fail; }
        if (!cJSON_AddItemToArray(enabled, on)) { cJSON_Delete(on); goto fail; }
    }
    NUM(ota, "mode", s->ota.mode); NUM(ota, "interval_hours", s->ota.interval_hours);
    NUM(ota, "schedule", s->ota.schedule); NUM(ota, "install_minute", s->ota.install_minute);
    STR(ota, "time_zone", s->ota.timezone);
    if (!cJSON_AddBoolToObject(ota, "timezone_auto", s->ota.timezone_auto)) goto fail;
#undef STR
#undef NUM
    return root;
fail:
    app_backup_json_delete(root);
    return NULL;
}

static const cJSON *get(const cJSON *json, const char *key) { return cJSON_GetObjectItemCaseSensitive(json, key); }
static bool string(const cJSON *json, const char *key, char *out, size_t size)
{
    const cJSON *value = get(json, key);
    return cJSON_IsString(value) && app_config_copy_string(out, size, value->valuestring, false);
}
static bool number(const cJSON *json, const char *key, uint32_t maximum, uint32_t *out)
{
    const cJSON *value = get(json, key);
    if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0 && value->valuedouble <= maximum)) return false;
    *out = (uint32_t) value->valuedouble;
    return value->valuedouble == *out;
}

esp_err_t app_backup_parse_snapshot(const cJSON *root, app_settings_snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    app_light_settings_defaults(&s->lights);
    app_ota_settings_defaults(&s->ota);
    const cJSON *device = get(root, "device"), *wifi = get(root, "wifi"), *jenkins = get(root, "jenkins"),
                *admin = get(root, "admin"), *lights = get(root, "lights"), *ota = get(root, "ota");
    uint32_t value;
    char format[32];
    if (!cJSON_IsObject(root) || !string(root, "format", format, sizeof(format)) || strcmp(format, "ci-lights-config") ||
        !number(root, "schema", 1, &value) || value != 1 || !cJSON_IsObject(device) || !cJSON_IsObject(wifi) ||
        !cJSON_IsObject(jenkins) || !cJSON_IsObject(admin) || !cJSON_IsObject(lights) || !cJSON_IsObject(ota)) return ESP_ERR_INVALID_ARG;
#define STRING(o,k,v) do { if (!string(o,k,v,sizeof(v))) return ESP_ERR_INVALID_ARG; } while (0)
#define NUMBER(o,k,v,max) do { if (!number(o,k,max,&value)) return ESP_ERR_INVALID_ARG; v=value; } while (0)
    STRING(device, "title", s->title); STRING(device, "hostname", s->hostname); STRING(device, "language", s->language);
    NUMBER(device, "cpu_mode", s->config.cpu_mode, APP_CPU_MODE_COUNT - 1);
    NUMBER(wifi, "last_index", s->wifi.last_index, APP_WIFI_PROFILE_MAX_COUNT - 1);
    const cJSON *networks = get(wifi, "networks");
    int count = cJSON_GetArraySize(networks);
    if (!cJSON_IsArray(networks) || count < 1 || count > APP_WIFI_PROFILE_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    s->wifi.count = count;
    for (int i = 0; i < count; ++i) {
        const cJSON *entry = cJSON_GetArrayItem(networks, i);
        app_wifi_profile_t *profile = &s->wifi.entries[i];
        if (!cJSON_IsObject(entry)) return ESP_ERR_INVALID_ARG;
        STRING(entry, "ssid", profile->ssid); STRING(entry, "username", profile->username); STRING(entry, "password", profile->password);
        NUMBER(entry, "auth", profile->auth, APP_WIFI_AUTH_COUNT - 1);
    }
    if (s->wifi.last_index >= s->wifi.count) return ESP_ERR_INVALID_ARG;
    const app_wifi_profile_t *preferred = &s->wifi.entries[s->wifi.last_index];
    strcpy(s->config.wifi_ssid, preferred->ssid); strcpy(s->config.wifi_username, preferred->username);
    strcpy(s->config.wifi_password, preferred->password); s->config.wifi_auth = preferred->auth;
    STRING(jenkins, "url", s->config.jenkins_url); STRING(jenkins, "job_path", s->config.jenkins_job_path);
    STRING(jenkins, "user", s->config.jenkins_user); STRING(jenkins, "token", s->config.jenkins_token);
    NUMBER(jenkins, "poll_interval_minutes", s->config.jenkins_poll_interval_minutes, UINT32_MAX);
    NUMBER(jenkins, "job_list_timeout_seconds", s->config.jenkins_job_list_timeout_seconds, APP_JENKINS_TIMEOUT_MAX_SECONDS);
    NUMBER(jenkins, "status_timeout_seconds", s->config.jenkins_status_timeout_seconds, APP_JENKINS_TIMEOUT_MAX_SECONDS);
    STRING(admin, "username", s->admin_username);
    size_t size;
    if (app_backup_unbase64(get(admin, "salt"), s->admin_salt, sizeof(s->admin_salt), &size) || size != sizeof(s->admin_salt) ||
        app_backup_unbase64(get(admin, "verifier"), s->admin_hash, sizeof(s->admin_hash), &size) || size != sizeof(s->admin_hash)) return ESP_ERR_INVALID_ARG;
    NUMBER(lights, "brightness", s->config.light_brightness_percent, 100);
    NUMBER(lights, "build_effect", s->config.build_effect, APP_BUILD_EFFECT_COUNT - 1);
    NUMBER(lights, "mode", s->lights.mode, APP_CONTROL_MODE_API);
    NUMBER(lights, "disco_effect", s->lights.disco_effect, APP_DISCO_EFFECT_COUNT - 1);
    const cJSON *colors = get(lights, "colors"), *enabled = get(lights, "enabled");
    if (!cJSON_IsArray(colors) || !cJSON_IsArray(enabled) || cJSON_GetArraySize(colors) != 3 || cJSON_GetArraySize(enabled) != 3) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 3; ++i) {
        const cJSON *color = cJSON_GetArrayItem(colors, i), *on = cJSON_GetArrayItem(enabled, i);
        if (!cJSON_IsString(color) || strlen(color->valuestring) != 7 || color->valuestring[0] != '#' ||
            strspn(color->valuestring + 1, "0123456789abcdefABCDEF") != 6 || !cJSON_IsBool(on)) return ESP_ERR_INVALID_ARG;
        unsigned long rgb = strtoul(color->valuestring + 1, NULL, 16);
        s->lights.colors[i][0] = rgb >> 16; s->lights.colors[i][1] = rgb >> 8; s->lights.colors[i][2] = rgb;
        s->lights.enabled[i] = cJSON_IsTrue(on);
    }
    NUMBER(ota, "mode", s->ota.mode, APP_OTA_AUTO_INSTALL);
    NUMBER(ota, "interval_hours", s->ota.interval_hours, 24);
    NUMBER(ota, "schedule", s->ota.schedule, APP_OTA_SCHEDULE_IMMEDIATE);
    NUMBER(ota, "install_minute", s->ota.install_minute, 1439);
    STRING(ota, "time_zone", s->ota.timezone);
    if (!cJSON_IsBool(get(ota, "timezone_auto"))) return ESP_ERR_INVALID_ARG;
    s->ota.timezone_auto = cJSON_IsTrue(get(ota, "timezone_auto"));
#undef STRING
#undef NUMBER
    return app_settings_snapshot_valid(s) ? ESP_OK : ESP_ERR_INVALID_ARG;
}
