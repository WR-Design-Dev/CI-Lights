#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"

#include "esp_err.h"

#define APP_WIFI_PROFILE_MAX_COUNT 8
#define APP_SITE_TITLE_MAX_LENGTH 128
#define APP_ADMIN_USERNAME_MIN_LENGTH 3
#define APP_ADMIN_USERNAME_MAX_LENGTH 32
#define APP_ADMIN_PASSWORD_MIN_LENGTH 8
#define APP_ADMIN_PASSWORD_MAX_LENGTH 64

typedef struct {
    char ssid[APP_WIFI_SSID_MAX_LENGTH + 1];
    char username[APP_WIFI_USERNAME_MAX_LENGTH + 1];
    char password[APP_WIFI_PASSWORD_MAX_LENGTH + 1];
} app_wifi_profile_t;

typedef struct {
    app_wifi_profile_t entries[APP_WIFI_PROFILE_MAX_COUNT];
    uint8_t count;
    uint8_t last_index;
} app_wifi_profiles_t;

/* Persistente Geraeteeinstellungen und gemeinsame Eingabepruefung. */
bool app_config_copy_string(char *destination, size_t destination_size,
                            const char *source, bool required);
bool app_config_wifi_is_valid(const app_config_t *config);
bool app_config_jenkins_is_valid(const app_config_t *config);
bool app_admin_password_is_set(void);
esp_err_t app_admin_credentials_save(const char *username, const char *password);
bool app_admin_credentials_verify(const char *username, const char *password);
esp_err_t app_config_save_wifi(const app_config_t *config);
bool app_wifi_profiles_load(app_wifi_profiles_t *profiles);
esp_err_t app_wifi_profiles_mark_success(uint8_t index);
esp_err_t app_wifi_profiles_move(const char *ssid, int8_t direction);
esp_err_t app_wifi_profiles_remove(const char *ssid);
esp_err_t app_config_save_jenkins(const app_config_t *config);
esp_err_t app_config_save_jenkins_poll_interval(uint32_t poll_interval_minutes);
esp_err_t app_config_save_light_brightness(uint8_t percent);
esp_err_t app_config_save_build_effect(app_build_effect_t effect);
esp_err_t app_config_save_cpu_mode(app_cpu_mode_t mode);
const char *app_config_get_language(void);
esp_err_t app_config_save_language(const char *language);
const char *app_config_get_site_title(void);
esp_err_t app_config_save_site_title(const char *title);
