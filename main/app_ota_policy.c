#include "app_ota_policy.h"
#include "app_timezones.h"
#include <string.h>

void app_ota_settings_defaults(app_ota_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->schema = 1;
    settings->interval_hours = 24;
    settings->install_minute = 180;
}

bool app_ota_settings_valid(const app_ota_settings_t *settings)
{
    if (settings == NULL || settings->schema != 1 || settings->mode > APP_OTA_AUTO_INSTALL ||
        settings->schedule > APP_OTA_SCHEDULE_IMMEDIATE || settings->install_minute >= 1440 ||
        (settings->interval_hours != 1 && settings->interval_hours != 6 &&
         settings->interval_hours != 12 && settings->interval_hours != 24) ||
        memchr(settings->timezone, '\0', sizeof(settings->timezone)) == NULL) return false;
    if (settings->timezone[0] != '\0' && !app_timezone_is_valid(settings->timezone)) return false;
    return settings->mode != APP_OTA_AUTO_INSTALL || settings->schedule != APP_OTA_SCHEDULE_DAILY ||
           settings->timezone[0] != '\0';
}

bool app_ota_parse_version(const char *text, uint32_t parts[3])
{
    if (text == NULL || *text == '\0' || strlen(text) >= APP_OTA_VERSION_LENGTH) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') return false;
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            unsigned digit = (unsigned) (*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        parts[i] = value;
        if (i < 2 && *text++ != '.') return false;
    }
    return *text == '\0';
}

bool app_ota_version_newer(const char *candidate, const char *current)
{
    uint32_t next[3], installed[3];
    if (!app_ota_parse_version(candidate, next) || !app_ota_parse_version(current, installed)) return false;
    for (unsigned i = 0; i < 3; ++i) if (next[i] != installed[i]) return next[i] > installed[i];
    return false;
}

bool app_ota_auto_version_allowed(const char *candidate, const char *blocked)
{
    uint32_t parts[3];
    return app_ota_parse_version(candidate, parts) && (blocked == NULL || *blocked == '\0' ||
                                                     app_ota_version_newer(candidate, blocked));
}

uint32_t app_ota_local_day(const char *zone, int64_t now)
{
    struct tm local;
    if (!app_timezone_localtime(zone, now, &local)) return 0;
    return (uint32_t) (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

int64_t app_ota_next_install(const app_ota_settings_t *settings, int64_t now, uint32_t last_day)
{
    int32_t offset;
    if (!app_ota_settings_valid(settings) || settings->mode != APP_OTA_AUTO_INSTALL ||
        settings->schedule != APP_OTA_SCHEDULE_DAILY ||
        !app_timezone_offset(settings->timezone, now, &offset)) return 0;
    int64_t local_midnight = ((now + offset) / 86400) * 86400;
    int64_t target;
    if (!app_timezone_resolve_local(settings->timezone, local_midnight + settings->install_minute * 60, &target)) return 0;
    if (last_day != app_ota_local_day(settings->timezone, now) && now < target + APP_OTA_INSTALL_WINDOW_SECONDS) return target;
    if (!app_timezone_resolve_local(settings->timezone, local_midnight + 86400 + settings->install_minute * 60, &target)) return 0;
    return target;
}

bool app_ota_install_due(const app_ota_settings_t *settings, int64_t now, uint32_t last_day)
{
    int64_t next = app_ota_next_install(settings, now, last_day);
    return next > 0 && now >= next && now < next + APP_OTA_INSTALL_WINDOW_SECONDS;
}

uint32_t app_ota_retry_seconds(unsigned failures)
{
    if (failures <= 1) return 15 * 60;
    if (failures == 2) return 60 * 60;
    return 6 * 60 * 60;
}

bool app_ota_history_finish_boot(app_ota_history_t *history, const char *current, int64_t now)
{
    uint32_t parts[3];
    if (history == NULL || !history->pending_version[0] || !app_ota_parse_version(current, parts)) return false;
    if (strcmp(history->pending_version, current) == 0) {
        strcpy(history->installed_version, current);
        history->installed_at = now;
        history->result = APP_OTA_RESULT_INSTALLED;
        if (strcmp(history->blocked_version, current) == 0) history->blocked_version[0] = '\0';
    } else {
        if (!history->blocked_version[0] || app_ota_version_newer(history->pending_version, history->blocked_version)) {
            strcpy(history->blocked_version, history->pending_version);
        }
        history->result = APP_OTA_RESULT_ROLLED_BACK;
    }
    history->pending_version[0] = '\0';
    return true;
}
