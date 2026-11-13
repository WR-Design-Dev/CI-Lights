#pragma once
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"
#include "app_settings.h"

#define APP_BACKUP_MAX_FILE_BYTES (160 * 1024)
#define APP_BACKUP_MAX_PLAIN_BYTES (112 * 1024)
#define APP_BACKUP_PASSWORD_MIN 12
#define APP_BACKUP_PASSWORD_MAX 128

void app_backup_json_delete(cJSON *json);
cJSON *app_backup_json_parse(const char *text);
char *app_backup_base64(const void *data, size_t size);
esp_err_t app_backup_unbase64(const cJSON *value, void *data, size_t capacity, size_t *size);
cJSON *app_backup_snapshot_json(const app_settings_snapshot_t *snapshot);
esp_err_t app_backup_parse_snapshot(const cJSON *json, app_settings_snapshot_t *snapshot);
esp_err_t app_backup_seal(const char *password, const char *plain, cJSON **sealed);
esp_err_t app_backup_open(const char *password, const cJSON *sealed, char **plain);
bool app_backup_password_valid(const char *password);

/* Caller reserves OTA and has authenticated the HTTP request. */
esp_err_t app_backup_export(const char *password, cJSON **sealed);
esp_err_t app_backup_validate(const char *password, const cJSON *sealed, cJSON **summary);
esp_err_t app_backup_import(const char *password, const cJSON *sealed);
