#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_OTA_VERSION_LENGTH 32
#define APP_OTA_TIMEZONE_LENGTH 64
#define APP_OTA_STARTUP_DELAY_SECONDS 300
#define APP_OTA_INSTALL_WINDOW_SECONDS 300

typedef enum { APP_OTA_AUTO_OFF, APP_OTA_AUTO_CHECK, APP_OTA_AUTO_INSTALL } app_ota_auto_mode_t;
typedef enum { APP_OTA_SCHEDULE_DAILY, APP_OTA_SCHEDULE_IMMEDIATE } app_ota_schedule_t;
typedef struct {
    uint8_t schema;
    uint8_t mode;
    uint8_t interval_hours;
    uint8_t schedule;
    uint16_t install_minute;
    bool timezone_auto;
    char timezone[APP_OTA_TIMEZONE_LENGTH];
} app_ota_settings_t;

typedef enum {
    APP_OTA_RESULT_NONE, APP_OTA_RESULT_CURRENT, APP_OTA_RESULT_AVAILABLE,
    APP_OTA_RESULT_CHECK_FAILED, APP_OTA_RESULT_INSTALL_FAILED,
    APP_OTA_RESULT_INSTALLED, APP_OTA_RESULT_ROLLED_BACK,
} app_ota_result_t;

typedef struct {
    uint8_t schema;
    uint8_t result;
    char pending_version[APP_OTA_VERSION_LENGTH];
    char blocked_version[APP_OTA_VERSION_LENGTH];
    char installed_version[APP_OTA_VERSION_LENGTH];
    int64_t checked_at;
    int64_t installed_at;
    uint32_t install_day;
} app_ota_history_t;

void app_ota_settings_defaults(app_ota_settings_t *settings);
bool app_ota_settings_valid(const app_ota_settings_t *settings);
bool app_ota_parse_version(const char *text, uint32_t parts[3]);
bool app_ota_version_newer(const char *candidate, const char *current);
bool app_ota_auto_version_allowed(const char *candidate, const char *blocked);
uint32_t app_ota_local_day(const char *zone, int64_t now);
int64_t app_ota_next_install(const app_ota_settings_t *settings, int64_t now, uint32_t last_day);
bool app_ota_install_due(const app_ota_settings_t *settings, int64_t now, uint32_t last_day);
uint32_t app_ota_retry_seconds(unsigned failures);
bool app_ota_history_finish_boot(app_ota_history_t *history, const char *current, int64_t now);
