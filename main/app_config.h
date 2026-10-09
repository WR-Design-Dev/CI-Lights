#pragma once

#include <stdbool.h>
#include <stdint.h>

#define APP_WIFI_SSID_MAX_LENGTH 32
#define APP_WIFI_USERNAME_MAX_LENGTH 127
#define APP_WIFI_PASSWORD_MAX_LENGTH 63
#define APP_JENKINS_URL_MAX_LENGTH 127
#define APP_JENKINS_JOB_PATH_MAX_LENGTH 255
#define APP_JENKINS_USER_MAX_LENGTH 63
#define APP_JENKINS_TOKEN_MAX_LENGTH 127
#define APP_JENKINS_POLL_INTERVAL_DEFAULT_MINUTES 5
#define APP_LIGHT_BRIGHTNESS_DEFAULT_PERCENT 50

typedef enum {
    APP_CPU_MODE_FIXED_160,
    APP_CPU_MODE_AUTO_160,
    APP_CPU_MODE_AUTO_240,
    APP_CPU_MODE_FIXED_240,
    APP_CPU_MODE_COUNT,
} app_cpu_mode_t;

typedef enum {
    APP_BUILD_EFFECT_PULSE,
    APP_BUILD_EFFECT_BLINK,
    APP_BUILD_EFFECT_COUNT,
} app_build_effect_t;

typedef enum {
    APP_WIFI_AUTH_AUTO,
    APP_WIFI_AUTH_PERSONAL,
    APP_WIFI_AUTH_ENTERPRISE,
    APP_WIFI_AUTH_COUNT,
} app_wifi_auth_t;

typedef struct {
    char wifi_ssid[APP_WIFI_SSID_MAX_LENGTH + 1];
    char wifi_username[APP_WIFI_USERNAME_MAX_LENGTH + 1];
    char wifi_password[APP_WIFI_PASSWORD_MAX_LENGTH + 1];
    app_wifi_auth_t wifi_auth;
    char jenkins_url[APP_JENKINS_URL_MAX_LENGTH + 1];
    char jenkins_job_path[APP_JENKINS_JOB_PATH_MAX_LENGTH + 1];
    char jenkins_user[APP_JENKINS_USER_MAX_LENGTH + 1];
    char jenkins_token[APP_JENKINS_TOKEN_MAX_LENGTH + 1];
    uint32_t jenkins_poll_interval_minutes;
    uint8_t light_brightness_percent;
    app_build_effect_t build_effect;
    app_cpu_mode_t cpu_mode;
} app_config_t;

typedef void (*app_job_selected_handler_t)(const app_config_t *config);

typedef enum {
    APP_MANUAL_LIGHT_RED,
    APP_MANUAL_LIGHT_YELLOW,
    APP_MANUAL_LIGHT_GREEN,
} app_manual_light_t;

typedef enum {
    APP_CONTROL_MODE_AUTO,
    APP_CONTROL_MODE_MANUAL,
    APP_CONTROL_MODE_DISCO,
    APP_CONTROL_MODE_API,
} app_control_mode_t;

typedef enum {
    APP_DISCO_EFFECT_RAINBOW,
    APP_DISCO_EFFECT_COLORLOOP,
    APP_DISCO_EFFECT_CHASE,
    APP_DISCO_EFFECT_RAINBOW_CHASE,
    APP_DISCO_EFFECT_BLINK,
    APP_DISCO_EFFECT_BREATHE,
    APP_DISCO_EFFECT_TWINKLE,
    APP_DISCO_EFFECT_SCAN,
    APP_DISCO_EFFECT_THEATER_CHASE,
    APP_DISCO_EFFECT_FIREWORKS,
    APP_DISCO_EFFECT_COUNT,
} app_disco_effect_t;

typedef enum {
    APP_LIGHT_STATUS_OFF,
    APP_LIGHT_STATUS_RED,
    APP_LIGHT_STATUS_YELLOW,
    APP_LIGHT_STATUS_GREEN,
} app_light_status_t;

typedef void (*app_manual_light_handler_t)(app_manual_light_t light, bool enabled);
typedef bool (*app_manual_light_state_handler_t)(app_manual_light_t light);
typedef void (*app_control_mode_handler_t)(app_control_mode_t mode);
typedef app_control_mode_t (*app_control_mode_state_handler_t)(void);
typedef void (*app_api_status_handler_t)(app_light_status_t status);
typedef void (*app_disco_effect_handler_t)(app_disco_effect_t effect);
typedef app_disco_effect_t (*app_disco_effect_state_handler_t)(void);
typedef void (*app_light_brightness_handler_t)(uint8_t percent);
typedef uint8_t (*app_light_brightness_state_handler_t)(void);

void app_storage_init(void);
bool app_config_factory_reset(void);
void app_network_init(void);
bool app_config_load(app_config_t *config);
bool app_connect_to_wifi(const app_config_t *config, uint32_t timeout_ms);
void app_stop_wifi(void);
void app_start_provisioning(void);
bool app_config_web_ready(void);
void app_start_status_server(const app_config_t *config,
                             app_job_selected_handler_t job_selected_handler,
                             app_manual_light_handler_t manual_light_handler,
                             app_manual_light_state_handler_t manual_light_state_handler,
                             app_control_mode_handler_t control_mode_handler,
                             app_control_mode_state_handler_t control_mode_state_handler,
                             app_api_status_handler_t api_status_handler,
                             app_disco_effect_handler_t disco_effect_handler,
                             app_disco_effect_state_handler_t disco_effect_state_handler,
                             app_light_brightness_handler_t light_brightness_handler,
                             app_light_brightness_state_handler_t light_brightness_state_handler);
