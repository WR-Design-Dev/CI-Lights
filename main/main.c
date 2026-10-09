#include "app_config.h"
#include "app_cpu.h"
#include "app_ota.h"
#include "app_settings.h"
#include "factory_reset.h"
#include "jenkins_client.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "traffic_light.h"

#include <string.h>

static const char *TAG = "main";

static void *json_malloc(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void json_free(void *pointer)
{
    heap_caps_free(pointer);
}

void app_main(void)
{
    /* Keep connection/setup addresses visible while other components stay quiet. */
    esp_log_level_set("network", ESP_LOG_INFO);
    esp_log_level_set("provision", ESP_LOG_INFO);
    cJSON_Hooks json_hooks = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&json_hooks);
    app_storage_init();
    app_ota_init();
    esp_err_t cpu_err = app_cpu_init();
    if (cpu_err != ESP_OK) {
        ESP_LOGW(TAG, "CPU-Leistungssperre konnte nicht eingerichtet werden: %s",
                 esp_err_to_name(cpu_err));
    }
    traffic_light_init();
    factory_reset_button_init();
    app_network_init();

    if (!app_admin_password_is_set()) {
        ESP_LOGW(TAG, "Verwaltungspasswort fehlt; starte die Einrichtung");
        app_start_provisioning();
        app_ota_confirm_startup(app_config_web_ready());
        return;
    }

    app_config_t config = {0};
    app_wifi_profiles_t *wifi_profiles = heap_caps_malloc_prefer(
        sizeof(*wifi_profiles), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (wifi_profiles == NULL || !app_config_load(&config) ||
        !app_wifi_profiles_load(wifi_profiles)) {
        heap_caps_free(wifi_profiles);
        app_start_provisioning();
        app_ota_confirm_startup(app_config_web_ready());
        return;
    }

    cpu_err = app_cpu_apply_mode(config.cpu_mode);
    if (cpu_err != ESP_OK) {
        ESP_LOGE(TAG, "CPU-Modus konnte nicht aktiviert werden: %s", esp_err_to_name(cpu_err));
        config.cpu_mode = app_cpu_current_mode();
    }

    traffic_light_set_brightness(config.light_brightness_percent);
    traffic_light_set_build_effect(config.build_effect);

    bool wifi_connected = false;
    for (uint8_t attempt = 0; attempt < wifi_profiles->count; ++attempt) {
        uint8_t index = attempt;
        const app_wifi_profile_t *profile = &wifi_profiles->entries[index];
        memcpy(config.wifi_ssid, profile->ssid, sizeof(config.wifi_ssid));
        memcpy(config.wifi_username, profile->username, sizeof(config.wifi_username));
        memcpy(config.wifi_password, profile->password, sizeof(config.wifi_password));
        config.wifi_auth = profile->auth;
        uint32_t timeout_ms = wifi_profiles->count == 1 ? 45000 : 30000;
        if (app_connect_to_wifi(&config, timeout_ms)) {
            wifi_connected = true;
            esp_err_t save_err = app_wifi_profiles_mark_success(index);
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Zuletzt verwendetes WLAN konnte nicht gespeichert werden: %s",
                         esp_err_to_name(save_err));
            }
            break;
        }
        app_stop_wifi();
    }
    heap_caps_free(wifi_profiles);
    if (!wifi_connected) {
        ESP_LOGE(TAG, "Keines der gespeicherten WLANs erreichbar");
        traffic_light_set(TRAFFIC_LIGHT_RED);
        app_start_provisioning();
        app_ota_confirm_startup(app_config_web_ready());
        return;
    }

    app_start_status_server(&config, jenkins_client_request, traffic_light_set_manual,
                            traffic_light_manual_state, jenkins_client_set_control_mode,
                            traffic_light_control_mode, traffic_light_set_api_status,
                            traffic_light_set_disco_effect, traffic_light_disco_effect,
                            traffic_light_set_brightness, traffic_light_brightness);
    jenkins_client_start_polling();
    app_ota_confirm_startup(app_config_web_ready());

    if (config.jenkins_url[0] == '\0') {
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
        return;
    }
    if (config.jenkins_job_path[0] == '\0') {
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
        return;
    }

}
