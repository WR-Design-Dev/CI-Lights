#include "app_cpu.h"

#include <string.h>

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_wifi.h"

static const char *TAG = "app_cpu";
static esp_pm_lock_handle_t s_work_lock;
static app_cpu_mode_t s_mode = APP_CPU_MODE_FIXED_160;

static const char *const MODE_NAMES[APP_CPU_MODE_COUNT] = {
    [APP_CPU_MODE_FIXED_160] = "fixed160",
    [APP_CPU_MODE_AUTO_160] = "auto160",
    [APP_CPU_MODE_AUTO_240] = "auto240",
    [APP_CPU_MODE_FIXED_240] = "fixed240",
};

esp_err_t app_cpu_init(void)
{
    return esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ci_lights_work", &s_work_lock);
}

esp_err_t app_cpu_apply_mode(app_cpu_mode_t mode)
{
    if (mode >= APP_CPU_MODE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    bool automatic = mode == APP_CPU_MODE_AUTO_160 || mode == APP_CPU_MODE_AUTO_240;
    if (automatic && s_work_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int maximum = mode == APP_CPU_MODE_AUTO_240 || mode == APP_CPU_MODE_FIXED_240 ? 240 : 160;
    esp_pm_config_t config = {
        .max_freq_mhz = maximum,
        .min_freq_mhz = automatic ? 40 : maximum,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&config);
    if (err != ESP_OK) {
        return err;
    }

    wifi_mode_t wifi_mode;
    if (esp_wifi_get_mode(&wifi_mode) == ESP_OK && wifi_mode == WIFI_MODE_STA) {
        err = esp_wifi_set_ps(automatic ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
        if (err != ESP_OK) {
            int previous_maximum = s_mode == APP_CPU_MODE_AUTO_240 ||
                                   s_mode == APP_CPU_MODE_FIXED_240 ? 240 : 160;
            bool previous_automatic = s_mode == APP_CPU_MODE_AUTO_160 ||
                                      s_mode == APP_CPU_MODE_AUTO_240;
            esp_pm_config_t previous = {
                .max_freq_mhz = previous_maximum,
                .min_freq_mhz = previous_automatic ? 40 : previous_maximum,
                .light_sleep_enable = false,
            };
            esp_err_t rollback_err = esp_pm_configure(&previous);
            if (rollback_err != ESP_OK) {
                ESP_LOGE(TAG, "Vorheriger CPU-Takt konnte nicht wiederhergestellt werden: %s",
                         esp_err_to_name(rollback_err));
            }
            return err;
        }
    }
    s_mode = mode;
    return ESP_OK;
}

app_cpu_mode_t app_cpu_current_mode(void)
{
    return s_mode;
}

const char *app_cpu_mode_name(app_cpu_mode_t mode)
{
    return mode < APP_CPU_MODE_COUNT ? MODE_NAMES[mode] : MODE_NAMES[APP_CPU_MODE_FIXED_160];
}

bool app_cpu_mode_from_name(const char *name, app_cpu_mode_t *mode)
{
    if (name == NULL || mode == NULL) {
        return false;
    }
    for (app_cpu_mode_t value = APP_CPU_MODE_FIXED_160; value < APP_CPU_MODE_COUNT; value++) {
        if (strcmp(name, MODE_NAMES[value]) == 0) {
            *mode = value;
            return true;
        }
    }
    return false;
}

bool app_cpu_boost_begin(void)
{
    return s_work_lock != NULL && esp_pm_lock_acquire(s_work_lock) == ESP_OK;
}

void app_cpu_boost_end(bool boosted)
{
    if (boosted) {
        esp_pm_lock_release(s_work_lock);
    }
}
