#include "app_settings.h"
#include "app_config_store.h"
#include "app_ota_storage.h"
#include "app_security.h"
#include "esp_partition.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NAMESPACE app_settings_namespace()
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_USERNAME "wifi_user"
#define NVS_KEY_WIFI_PASSWORD "wifi_password"
#define NVS_KEY_WIFI_AUTH "wifi_auth"
#define NVS_KEY_JENKINS_URL "jenkins_url"
#define NVS_KEY_JENKINS_JOB_PATH "jenkins_job"
#define NVS_KEY_JENKINS_USER "jenkins_user"
#define NVS_KEY_JENKINS_TOKEN "jenkins_token"
#define NVS_KEY_JENKINS_POLL_INTERVAL "jenkins_poll"
#define NVS_KEY_JENKINS_LIST_TIMEOUT "jenkins_list_to"
#define NVS_KEY_JENKINS_STATUS_TIMEOUT "jenkins_stat_to"
#define NVS_KEY_LIGHT_BRIGHTNESS "brightness"
#define NVS_KEY_BUILD_EFFECT "build_effect"
#define NVS_KEY_CPU_MODE "cpu_mode"
#define NVS_KEY_UI_LANGUAGE "ui_language"
#define NVS_KEY_SITE_TITLE "site_title"
#define NVS_KEY_HOSTNAME "hostname"
#define NVS_KEY_WIFI_COUNT "wifi_count"
#define NVS_KEY_WIFI_LAST "wifi_last"
#define NVS_KEY_WIFI_ORDERED "wifi_ordered"
#define NVS_KEY_ADMIN_SALT "admin_salt"
#define NVS_KEY_ADMIN_HASH "admin_hash"
#define NVS_KEY_ADMIN_USERNAME "admin_user"

#define ADMIN_SALT_LENGTH 16
#define ADMIN_HASH_LENGTH 32

static const char *TAG = "settings";
static char s_ui_language[3] = "de";
static char s_site_title[APP_SITE_TITLE_MAX_LENGTH + 1] = "CI-Lights";
static char s_hostname[APP_HOSTNAME_MAX_LENGTH + 1];
static uint8_t s_admin_salt[ADMIN_SALT_LENGTH];
static uint8_t s_admin_hash[ADMIN_HASH_LENGTH];
static bool s_admin_password_set;
static char s_admin_username[APP_ADMIN_USERNAME_MAX_LENGTH + 1] = "admin";

static bool admin_username_valid(const char *username)
{
    if (username == NULL) {
        return false;
    }
    size_t length = strlen(username);
    if (length < APP_ADMIN_USERNAME_MIN_LENGTH ||
        length > APP_ADMIN_USERNAME_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char ch = username[i];
        bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool digit = ch >= '0' && ch <= '9';
        if (!letter && !digit && (i == 0 || (ch != '.' && ch != '_' && ch != '-'))) {
            return false;
        }
    }
    return true;
}

static bool admin_password_valid(const char *password)
{
    if (password == NULL) {
        return false;
    }
    size_t length = strlen(password);
    if (length < APP_ADMIN_PASSWORD_MIN_LENGTH ||
        length > APP_ADMIN_PASSWORD_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char) password[i] < 0x20 ||
            (unsigned char) password[i] == 0x7f) {
            return false;
        }
    }
    return true;
}

static bool admin_password_hash(const char *password, const uint8_t *salt,
                                uint8_t *hash)
{
    uint8_t input[ADMIN_SALT_LENGTH + APP_ADMIN_PASSWORD_MAX_LENGTH];
    size_t password_length = strlen(password);
    memcpy(input, salt, ADMIN_SALT_LENGTH);
    memcpy(input + ADMIN_SALT_LENGTH, password, password_length);
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok = sha256 != NULL &&
              mbedtls_md(sha256, input, ADMIN_SALT_LENGTH + password_length, hash) == 0;
    mbedtls_platform_zeroize(input, sizeof(input));
    return ok;
}

static esp_err_t save_wifi_profiles(const app_wifi_profiles_t *profiles);

static bool remove_duplicate_wifi_profiles(app_wifi_profiles_t *profiles)
{
    bool changed = false;
    for (uint8_t i = 0; i < profiles->count;) {
        uint8_t newer = i + 1;
        while (newer < profiles->count &&
               strcmp(profiles->entries[i].ssid, profiles->entries[newer].ssid) != 0) {
            ++newer;
        }
        if (newer == profiles->count) {
            ++i;
            continue;
        }
        if (profiles->last_index == i) {
            profiles->last_index = newer;
        }
        for (uint8_t index = i; index + 1 < profiles->count; ++index) {
            profiles->entries[index] = profiles->entries[index + 1];
        }
        memset(&profiles->entries[--profiles->count], 0, sizeof(profiles->entries[0]));
        if (profiles->last_index > i) {
            --profiles->last_index;
        }
        changed = true;
    }
    return changed;
}

static bool legacy_wifi_keys_present(nvs_handle_t handle)
{
    const char *keys[] = {NVS_KEY_WIFI_SSID, NVS_KEY_WIFI_USERNAME,
                          NVS_KEY_WIFI_PASSWORD};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        size_t length = 0;
        if (nvs_get_str(handle, keys[i], NULL, &length) == ESP_OK) {
            return true;
        }
    }
    return false;
}

/* NVS keys allow 15 characters; wifi_password plus '_' leaves one digit. */
_Static_assert(APP_WIFI_PROFILE_MAX_COUNT <= 10,
               "WLAN profile keys require single-digit indices");

static void wifi_profile_key(char key[16], const char *prefix, uint8_t index)
{
    snprintf(key, 16, "%s_%c", prefix, '0' + index);
}

static esp_err_t load_wifi_profile(nvs_handle_t handle, uint8_t index, bool indexed,
                                   app_wifi_profile_t *profile)
{
    if (indexed && index >= APP_WIFI_PROFILE_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    if (indexed) {
        wifi_profile_key(key, NVS_KEY_WIFI_SSID, index);
    }
    size_t length = sizeof(profile->ssid);
    esp_err_t err = nvs_get_str(handle, indexed ? key : NVS_KEY_WIFI_SSID,
                                profile->ssid, &length);
    if (err != ESP_OK || profile->ssid[0] == '\0') {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    if (indexed) {
        wifi_profile_key(key, NVS_KEY_WIFI_PASSWORD, index);
    }
    length = sizeof(profile->password);
    err = nvs_get_str(handle, indexed ? key : NVS_KEY_WIFI_PASSWORD,
                      profile->password, &length);
    if (err != ESP_OK) {
        return err;
    }
    if (indexed) {
        wifi_profile_key(key, NVS_KEY_WIFI_USERNAME, index);
    }
    length = sizeof(profile->username);
    err = nvs_get_str(handle, indexed ? key : NVS_KEY_WIFI_USERNAME,
                      profile->username, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        profile->username[0] = '\0';
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (indexed) {
        wifi_profile_key(key, NVS_KEY_WIFI_AUTH, index);
    }
    uint8_t auth = APP_WIFI_AUTH_AUTO;
    err = nvs_get_u8(handle, indexed ? key : NVS_KEY_WIFI_AUTH, &auth);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; // Existing profiles gain automatic security detection.
    }
    profile->auth = auth < APP_WIFI_AUTH_COUNT ? auth : APP_WIFI_AUTH_AUTO;
    return err;
}

bool app_config_copy_string(char *destination, size_t destination_size,
                            const char *source, bool required)
{
    if (source == NULL) {
        return false;
    }

    size_t length = strlen(source);
    if ((required && length == 0) || length >= destination_size) {
        return false;
    }

    memcpy(destination, source, length + 1);
    return true;
}

bool app_config_wifi_is_valid(const app_config_t *config)
{
    return config != NULL && config->wifi_ssid[0] != '\0' &&
           config->wifi_auth >= APP_WIFI_AUTH_AUTO && config->wifi_auth < APP_WIFI_AUTH_COUNT &&
           (config->wifi_auth != APP_WIFI_AUTH_ENTERPRISE ||
            (config->wifi_username[0] != '\0' && config->wifi_password[0] != '\0'));
}

bool app_config_jenkins_is_valid(const app_config_t *config)
{
    return config != NULL && strncmp(config->jenkins_url, "https://", 8) == 0 &&
           config->jenkins_user[0] != '\0' && config->jenkins_token[0] != '\0';
}

#if CONFIG_APP_SECURITY_SECURE
static bool nvs_partition_is_erased(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (partition == NULL) return false;
    uint8_t data[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(data)) {
        if (esp_partition_read(partition, offset, data, sizeof(data)) != ESP_OK) return false;
        for (size_t i = 0; i < sizeof(data); ++i) if (data[i] != 0xff) return false;
    }
    return true;
}
#endif

void app_storage_init(void)
{
#if CONFIG_APP_SECURITY_SECURE
    const esp_partition_t *keys = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                          ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    nvs_sec_cfg_t security;
    ESP_ERROR_CHECK(keys != NULL ? ESP_OK : ESP_ERR_NOT_FOUND);
    esp_err_t key_err = nvs_flash_read_security_cfg(keys, &security);
    mbedtls_platform_zeroize(&security, sizeof(security));
    /* Generate only for a fresh empty store. Never silently replace lost keys. */
    ESP_ERROR_CHECK(key_err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED && nvs_partition_is_erased() ? ESP_OK : key_err);
#endif
    esp_err_t err = nvs_flash_init();
    if (!CONFIG_APP_SECURITY_SECURE &&
        (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(app_config_store_init());
    nvs_handle_t nvs_handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t salt_length = sizeof(s_admin_salt);
        size_t hash_length = sizeof(s_admin_hash);
        s_admin_password_set =
            nvs_get_blob(nvs_handle, NVS_KEY_ADMIN_SALT, s_admin_salt,
                         &salt_length) == ESP_OK && salt_length == sizeof(s_admin_salt) &&
            nvs_get_blob(nvs_handle, NVS_KEY_ADMIN_HASH, s_admin_hash,
                         &hash_length) == ESP_OK && hash_length == sizeof(s_admin_hash);
        if (s_admin_password_set) {
            char saved_username[sizeof(s_admin_username)];
            size_t username_length = sizeof(saved_username);
            if (nvs_get_str(nvs_handle, NVS_KEY_ADMIN_USERNAME, saved_username,
                            &username_length) == ESP_OK &&
                admin_username_valid(saved_username)) {
                memcpy(s_admin_username, saved_username, username_length);
            }
        }
        char saved_language[sizeof(s_ui_language)];
        size_t length = sizeof(saved_language);
        if (nvs_get_str(nvs_handle, NVS_KEY_UI_LANGUAGE, saved_language, &length) == ESP_OK &&
            (strcmp(saved_language, "de") == 0 || strcmp(saved_language, "en") == 0)) {
            memcpy(s_ui_language, saved_language, sizeof(s_ui_language));
        }
        length = sizeof(s_site_title);
        if (nvs_get_str(nvs_handle, NVS_KEY_SITE_TITLE, s_site_title, &length) != ESP_OK ||
            s_site_title[0] == '\0') {
            strcpy(s_site_title, "CI-Lights");
        }
        char saved_hostname[sizeof(s_hostname)];
        length = sizeof(saved_hostname);
        if (nvs_get_str(nvs_handle, NVS_KEY_HOSTNAME, saved_hostname, &length) == ESP_OK) {
            if (!app_hostname_normalize(saved_hostname, s_hostname)) s_hostname[0] = '\0';
        }
        nvs_close(nvs_handle);
    }
}

bool app_admin_password_is_set(void)
{
    return s_admin_password_set;
}

esp_err_t app_admin_credentials_save(const char *username, const char *password)
{
    if (!admin_username_valid(username) || !admin_password_valid(password)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_admin_password_set) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t salt[ADMIN_SALT_LENGTH];
    uint8_t hash[ADMIN_HASH_LENGTH];
    esp_fill_random(salt, sizeof(salt));
    if (!admin_password_hash(password, salt, hash)) {
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(hash, sizeof(hash));
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY_ADMIN_SALT, salt, sizeof(salt));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_ADMIN_HASH, hash, sizeof(hash));
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_ADMIN_USERNAME, username);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        memcpy(s_admin_salt, salt, sizeof(s_admin_salt));
        memcpy(s_admin_hash, hash, sizeof(s_admin_hash));
        strcpy(s_admin_username, username);
        s_admin_password_set = true;
    }
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return err;
}

bool app_admin_credentials_verify(const char *username, const char *password)
{
    if (!s_admin_password_set || !admin_username_valid(username) ||
        strcmp(username, s_admin_username) != 0 || !admin_password_valid(password)) {
        return false;
    }
    uint8_t hash[ADMIN_HASH_LENGTH];
    if (!admin_password_hash(password, s_admin_salt, hash)) {
        return false;
    }
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < sizeof(hash); ++i) {
        difference |= hash[i] ^ s_admin_hash[i];
    }
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return difference == 0;
}

const char *app_config_get_language(void)
{
    return s_ui_language;
}

esp_err_t app_config_save_language(const char *language)
{
    if (language == NULL ||
        (strcmp(language, "de") != 0 && strcmp(language, "en") != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs_handle, NVS_KEY_UI_LANGUAGE, language);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    if (err == ESP_OK) {
        memcpy(s_ui_language, language, sizeof(s_ui_language));
    }
    return err;
}

const char *app_config_get_site_title(void)
{
    return s_site_title;
}

esp_err_t app_config_save_site_title(const char *title)
{
    if (title == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t length = strlen(title);
    if (length == 0 || length > APP_SITE_TITLE_MAX_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    bool visible = false;
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char) title[i] < 0x20 || (unsigned char) title[i] == 0x7f) {
            return ESP_ERR_INVALID_ARG;
        }
        if ((unsigned char) title[i] > 0x20) {
            visible = true;
        }
    }
    if (!visible) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs_handle, NVS_KEY_SITE_TITLE, title);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    if (err == ESP_OK) {
        memcpy(s_site_title, title, length + 1);
    }
    return err;
}

const char *app_config_get_hostname(void) { return s_hostname; }

esp_err_t app_config_save_hostname(const char *hostname)
{
    char normalized[sizeof(s_hostname)];
    if (!app_hostname_normalize(hostname, normalized)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, NVS_KEY_HOSTNAME, normalized);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) memcpy(s_hostname, normalized, strlen(normalized) + 1);
    return err;
}

bool app_config_factory_reset(void)
{
    esp_err_t err = app_config_store_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Einstellungen konnten nicht zurückgesetzt werden: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGW(TAG, "WLAN- und Jenkins-Einstellungen wurden zurückgesetzt");
    return true;
}

bool app_config_load(app_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->jenkins_poll_interval_minutes = APP_JENKINS_POLL_INTERVAL_DEFAULT_MINUTES;
    config->jenkins_job_list_timeout_seconds = APP_JENKINS_TIMEOUT_DEFAULT_SECONDS;
    config->jenkins_status_timeout_seconds = APP_JENKINS_TIMEOUT_DEFAULT_SECONDS;
    config->light_brightness_percent = APP_LIGHT_BRIGHTNESS_DEFAULT_PERCENT;
    config->build_effect = APP_BUILD_EFFECT_PULSE;
    config->cpu_mode = APP_CPU_MODE_FIXED_160;

    nvs_handle_t nvs_handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK) {
        return false;
    }

    uint8_t count = 0;
    uint8_t last_index = 0;
    esp_err_t count_err = nvs_get_u8(nvs_handle, NVS_KEY_WIFI_COUNT, &count);
    app_wifi_profile_t preferred = {0};
    esp_err_t err;
    if (count_err == ESP_OK && count > 0 && count <= APP_WIFI_PROFILE_MAX_COUNT) {
        if (nvs_get_u8(nvs_handle, NVS_KEY_WIFI_LAST, &last_index) != ESP_OK ||
            last_index >= count) {
            last_index = 0;
        }
        err = load_wifi_profile(nvs_handle, last_index, true, &preferred);
    } else if (count_err == ESP_ERR_NVS_NOT_FOUND) {
        err = load_wifi_profile(nvs_handle, 0, false, &preferred);
    } else {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    memcpy(config->wifi_ssid, preferred.ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_username, preferred.username, sizeof(config->wifi_username));
    memcpy(config->wifi_password, preferred.password, sizeof(config->wifi_password));
    config->wifi_auth = preferred.auth;

    size_t jenkins_url_size = sizeof(config->jenkins_url);
    size_t jenkins_user_size = sizeof(config->jenkins_user);
    size_t jenkins_token_size = sizeof(config->jenkins_token);
    size_t jenkins_job_path_size = sizeof(config->jenkins_job_path);
    esp_err_t jenkins_err = nvs_get_str(nvs_handle, NVS_KEY_JENKINS_URL,
                                        config->jenkins_url, &jenkins_url_size);
    if (jenkins_err == ESP_OK) {
        jenkins_err = nvs_get_str(nvs_handle, NVS_KEY_JENKINS_USER,
                                  config->jenkins_user, &jenkins_user_size);
    }
    if (jenkins_err == ESP_OK) {
        jenkins_err = nvs_get_str(nvs_handle, NVS_KEY_JENKINS_TOKEN,
                                  config->jenkins_token, &jenkins_token_size);
    }
    if (jenkins_err == ESP_OK) {
        jenkins_err = nvs_get_str(nvs_handle, NVS_KEY_JENKINS_JOB_PATH,
                                  config->jenkins_job_path, &jenkins_job_path_size);
        if (jenkins_err == ESP_ERR_NVS_NOT_FOUND) {
            config->jenkins_job_path[0] = '\0';
            jenkins_err = ESP_OK;
        }
    }

    uint32_t poll_interval_minutes = APP_JENKINS_POLL_INTERVAL_DEFAULT_MINUTES;
    esp_err_t poll_interval_err = nvs_get_u32(nvs_handle, NVS_KEY_JENKINS_POLL_INTERVAL,
                                              &poll_interval_minutes);
    if (poll_interval_err == ESP_OK && poll_interval_minutes >= 1) {
        config->jenkins_poll_interval_minutes = poll_interval_minutes;
    }

    uint32_t timeout_seconds;
    if (nvs_get_u32(nvs_handle, NVS_KEY_JENKINS_LIST_TIMEOUT, &timeout_seconds) == ESP_OK &&
        app_jenkins_timeout_is_valid(timeout_seconds)) {
        config->jenkins_job_list_timeout_seconds = timeout_seconds;
    }
    if (nvs_get_u32(nvs_handle, NVS_KEY_JENKINS_STATUS_TIMEOUT, &timeout_seconds) == ESP_OK &&
        app_jenkins_timeout_is_valid(timeout_seconds)) {
        config->jenkins_status_timeout_seconds = timeout_seconds;
    }

    uint8_t brightness_percent = APP_LIGHT_BRIGHTNESS_DEFAULT_PERCENT;
    esp_err_t brightness_err = nvs_get_u8(nvs_handle, NVS_KEY_LIGHT_BRIGHTNESS,
                                          &brightness_percent);
    if (brightness_err == ESP_OK && brightness_percent >= 1 && brightness_percent <= 100) {
        config->light_brightness_percent = brightness_percent;
    }
    uint8_t build_effect = APP_BUILD_EFFECT_PULSE;
    if (nvs_get_u8(nvs_handle, NVS_KEY_BUILD_EFFECT, &build_effect) == ESP_OK &&
        build_effect < APP_BUILD_EFFECT_COUNT) {
        config->build_effect = (app_build_effect_t) build_effect;
    }
    uint8_t cpu_mode = APP_CPU_MODE_FIXED_160;
    if (nvs_get_u8(nvs_handle, NVS_KEY_CPU_MODE, &cpu_mode) == ESP_OK &&
        cpu_mode < APP_CPU_MODE_COUNT) {
        config->cpu_mode = (app_cpu_mode_t) cpu_mode;
    }
    nvs_close(nvs_handle);

    if (jenkins_err != ESP_OK || !app_config_jenkins_is_valid(config)) {
        config->jenkins_url[0] = '\0';
        config->jenkins_user[0] = '\0';
        config->jenkins_token[0] = '\0';
        config->jenkins_job_path[0] = '\0';
    }
    return true;
}

bool app_wifi_profiles_load(app_wifi_profiles_t *profiles)
{
    if (profiles == NULL) {
        return false;
    }
    memset(profiles, 0, sizeof(*profiles));
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t count = 0;
    esp_err_t err = nvs_get_u8(handle, NVS_KEY_WIFI_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = load_wifi_profile(handle, 0, false, &profiles->entries[0]);
        nvs_close(handle);
        if (err != ESP_OK) {
            return false;
        }
        profiles->count = 1;
        esp_err_t save_err = save_wifi_profiles(profiles);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "Altes WLAN-Profil konnte nicht migriert werden: %s",
                     esp_err_to_name(save_err));
        }
        return true;
    }
    if (err != ESP_OK || count == 0 || count > APP_WIFI_PROFILE_MAX_COUNT) {
        nvs_close(handle);
        return false;
    }
    profiles->count = count;
    for (uint8_t i = 0; i < count && err == ESP_OK; ++i) {
        err = load_wifi_profile(handle, i, true, &profiles->entries[i]);
    }
    uint8_t last_index = 0;
    if (err == ESP_OK && nvs_get_u8(handle, NVS_KEY_WIFI_LAST, &last_index) == ESP_OK &&
        last_index < count) {
        profiles->last_index = last_index;
    }
    uint8_t ordered = 0;
    bool order_migration_needed = err == ESP_OK &&
                                  nvs_get_u8(handle, NVS_KEY_WIFI_ORDERED, &ordered) != ESP_OK;
    bool legacy_present = err == ESP_OK && legacy_wifi_keys_present(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }
    bool duplicates_removed = remove_duplicate_wifi_profiles(profiles);
    if (order_migration_needed) {
        /* Preserve the previous boot order once when upgrading: last used first. */
        uint8_t shifts = profiles->last_index;
        for (uint8_t i = 0; i < shifts; ++i) {
            app_wifi_profile_t first = profiles->entries[0];
            memmove(&profiles->entries[0], &profiles->entries[1],
                    (profiles->count - 1) * sizeof(profiles->entries[0]));
            profiles->entries[profiles->count - 1] = first;
        }
        profiles->last_index = 0;
    }
    if (legacy_present || duplicates_removed || order_migration_needed) {
        esp_err_t save_err = save_wifi_profiles(profiles);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "WLAN-Profile konnten nicht bereinigt werden: %s",
                     esp_err_to_name(save_err));
        }
    }
    return true;
}

static esp_err_t save_wifi_profiles(const app_wifi_profiles_t *profiles)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, NVS_KEY_WIFI_COUNT, profiles->count);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_WIFI_LAST, profiles->last_index);
    }
    const char *prefixes[] = {NVS_KEY_WIFI_SSID, NVS_KEY_WIFI_USERNAME,
                              NVS_KEY_WIFI_PASSWORD, NVS_KEY_WIFI_AUTH};
    for (uint8_t i = 0; i < APP_WIFI_PROFILE_MAX_COUNT && err == ESP_OK; ++i) {
        for (size_t field = 0; field < 4 && err == ESP_OK; ++field) {
            char key[16];
            wifi_profile_key(key, prefixes[field], i);
            if (i < profiles->count) {
                const app_wifi_profile_t *profile = &profiles->entries[i];
                if (field == 3) {
                    err = nvs_set_u8(handle, key, profile->auth);
                } else {
                    const char *value = field == 0 ? profile->ssid :
                                        field == 1 ? profile->username : profile->password;
                    err = nvs_set_str(handle, key, value);
                }
            } else {
                err = nvs_erase_key(handle, key);
                if (err == ESP_ERR_NVS_NOT_FOUND) {
                    err = ESP_OK;
                }
            }
        }
    }
    for (size_t field = 0; field < 4 && err == ESP_OK; ++field) {
        err = nvs_erase_key(handle, prefixes[field]);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_WIFI_ORDERED, 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t app_config_save_wifi(const app_config_t *config)
{
    if (!app_config_wifi_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    app_wifi_profiles_t profiles;
    if (!app_wifi_profiles_load(&profiles)) {
        memset(&profiles, 0, sizeof(profiles));
    }
    uint8_t index = 0;
    while (index < profiles.count &&
           strcmp(profiles.entries[index].ssid, config->wifi_ssid) != 0) {
        ++index;
    }
    if (index == profiles.count && profiles.count == APP_WIFI_PROFILE_MAX_COUNT) {
        index = profiles.count - 1;
        if (profiles.last_index == index) {
            profiles.last_index = 0;
        }
    } else if (index == profiles.count) {
        index = profiles.count++;
    }
    app_wifi_profile_t *profile = &profiles.entries[index];
    memcpy(profile->ssid, config->wifi_ssid, sizeof(profile->ssid));
    memcpy(profile->username, config->wifi_username, sizeof(profile->username));
    memcpy(profile->password, config->wifi_password, sizeof(profile->password));
    profile->auth = config->wifi_auth;
    return save_wifi_profiles(&profiles);
}

esp_err_t app_wifi_profiles_move(const char *ssid, int8_t direction)
{
    if (ssid == NULL || (direction != -1 && direction != 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    app_wifi_profiles_t profiles;
    if (!app_wifi_profiles_load(&profiles)) {
        return ESP_FAIL;
    }
    uint8_t index = 0;
    while (index < profiles.count && strcmp(profiles.entries[index].ssid, ssid) != 0) {
        ++index;
    }
    if (index == profiles.count) {
        return ESP_ERR_NOT_FOUND;
    }
    int target = (int) index + direction;
    if (target < 0 || target >= profiles.count) {
        return ESP_ERR_INVALID_STATE;
    }
    app_wifi_profile_t moved = profiles.entries[index];
    profiles.entries[index] = profiles.entries[target];
    profiles.entries[target] = moved;
    if (profiles.last_index == index) {
        profiles.last_index = target;
    } else if (profiles.last_index == target) {
        profiles.last_index = index;
    }
    return save_wifi_profiles(&profiles);
}

esp_err_t app_wifi_profiles_mark_success(uint8_t index)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t count = 0;
    uint8_t last = 0;
    err = nvs_get_u8(handle, NVS_KEY_WIFI_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND && index == 0) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK || index >= count) {
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_get_u8(handle, NVS_KEY_WIFI_LAST, &last) == ESP_OK && last == index) {
        nvs_close(handle);
        return ESP_OK;
    }
    err = nvs_set_u8(handle, NVS_KEY_WIFI_LAST, index);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t app_wifi_profiles_remove(const char *ssid)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    app_wifi_profiles_t profiles;
    if (!app_wifi_profiles_load(&profiles)) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t index = 0;
    while (index < profiles.count && strcmp(profiles.entries[index].ssid, ssid) != 0) {
        ++index;
    }
    if (index == profiles.count) {
        return ESP_ERR_NOT_FOUND;
    }
    if (profiles.count == 1) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t i = index; i + 1 < profiles.count; ++i) {
        profiles.entries[i] = profiles.entries[i + 1];
    }
    memset(&profiles.entries[--profiles.count], 0, sizeof(profiles.entries[0]));
    if (profiles.last_index == index) {
        profiles.last_index = 0;
    } else if (profiles.last_index > index) {
        --profiles.last_index;
    }
    return save_wifi_profiles(&profiles);
}

esp_err_t app_config_save_jenkins(const app_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_JENKINS_URL, config->jenkins_url);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, NVS_KEY_JENKINS_JOB_PATH,
                          config->jenkins_job_path);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, NVS_KEY_JENKINS_USER, config->jenkins_user);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, NVS_KEY_JENKINS_TOKEN, config->jenkins_token);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t app_config_save_jenkins_poll_settings(uint32_t poll_interval_minutes,
                                              uint32_t job_list_timeout_seconds,
                                              uint32_t status_timeout_seconds)
{
    if (poll_interval_minutes < 1 || !app_jenkins_timeout_is_valid(job_list_timeout_seconds) ||
        !app_jenkins_timeout_is_valid(status_timeout_seconds)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(nvs_handle, NVS_KEY_JENKINS_POLL_INTERVAL,
                      poll_interval_minutes);
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs_handle, NVS_KEY_JENKINS_LIST_TIMEOUT, job_list_timeout_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs_handle, NVS_KEY_JENKINS_STATUS_TIMEOUT, status_timeout_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t app_config_save_light_brightness(uint8_t percent)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs_handle, NVS_KEY_LIGHT_BRIGHTNESS, percent);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t app_config_save_build_effect(app_build_effect_t effect)
{
    if (effect >= APP_BUILD_EFFECT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs_handle, NVS_KEY_BUILD_EFFECT, (uint8_t) effect);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

esp_err_t app_config_save_cpu_mode(app_cpu_mode_t mode)
{
    if (mode >= APP_CPU_MODE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs_handle, NVS_KEY_CPU_MODE, (uint8_t) mode);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

void app_light_settings_defaults(app_light_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->schema = 1;
    settings->colors[0][0] = 255;
    settings->colors[1][0] = settings->colors[1][1] = 255;
    settings->colors[2][1] = 255;
}

bool app_light_settings_valid(const app_light_settings_t *settings)
{
    return settings != NULL && settings->schema == 1 && settings->mode <= APP_CONTROL_MODE_API &&
           settings->disco_effect < APP_DISCO_EFFECT_COUNT;
}

esp_err_t app_light_settings_load(app_light_settings_t *settings)
{
    app_light_settings_defaults(settings);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    app_light_settings_t stored;
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, "lights", &stored, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(stored) || !app_light_settings_valid(&stored)) return ESP_ERR_INVALID_STATE;
    *settings = stored;
    return ESP_OK;
}

esp_err_t app_light_settings_save(const app_light_settings_t *settings)
{
    if (!app_light_settings_valid(settings)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "lights", settings, sizeof(*settings));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_settings_snapshot_load(app_settings_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_admin_password_set || !app_config_load(&snapshot->config) ||
        !app_wifi_profiles_load(&snapshot->wifi)) return ESP_ERR_INVALID_STATE;
    strcpy(snapshot->admin_username, s_admin_username);
    memcpy(snapshot->admin_salt, s_admin_salt, sizeof(s_admin_salt));
    memcpy(snapshot->admin_hash, s_admin_hash, sizeof(s_admin_hash));
    strcpy(snapshot->language, s_ui_language);
    strcpy(snapshot->title, s_site_title);
    strcpy(snapshot->hostname, s_hostname);
    esp_err_t err = app_light_settings_load(&snapshot->lights);
    if (err == ESP_OK) err = app_ota_storage_load(&snapshot->ota, &snapshot->ota_history);
    return err;
}

static bool terminated(const char *value, size_t size) { return memchr(value, 0, size) != NULL; }

bool app_settings_snapshot_valid(const app_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    const app_config_t *config = &snapshot->config;
    if (!terminated(snapshot->admin_username, sizeof(snapshot->admin_username)) ||
        !admin_username_valid(snapshot->admin_username) ||
        !terminated(snapshot->language, sizeof(snapshot->language)) ||
        (strcmp(snapshot->language, "de") && strcmp(snapshot->language, "en")) ||
        !terminated(snapshot->title, sizeof(snapshot->title)) || !snapshot->title[0] ||
        !terminated(snapshot->hostname, sizeof(snapshot->hostname)) ||
        snapshot->wifi.count == 0 || snapshot->wifi.count > APP_WIFI_PROFILE_MAX_COUNT ||
        snapshot->wifi.last_index >= snapshot->wifi.count ||
        !config->jenkins_poll_interval_minutes || !app_jenkins_timeout_is_valid(config->jenkins_job_list_timeout_seconds) ||
        !app_jenkins_timeout_is_valid(config->jenkins_status_timeout_seconds) ||
        config->light_brightness_percent < 1 || config->light_brightness_percent > 100 ||
        (unsigned) config->cpu_mode >= APP_CPU_MODE_COUNT || (unsigned) config->build_effect >= APP_BUILD_EFFECT_COUNT ||
        !app_light_settings_valid(&snapshot->lights) || !app_ota_settings_valid(&snapshot->ota) ||
        !terminated(config->jenkins_url, sizeof(config->jenkins_url)) ||
        !terminated(config->jenkins_user, sizeof(config->jenkins_user)) ||
        !terminated(config->jenkins_token, sizeof(config->jenkins_token)) ||
        !terminated(config->jenkins_job_path, sizeof(config->jenkins_job_path))) return false;
    char hostname[APP_HOSTNAME_MAX_LENGTH + 1];
    if (!app_hostname_normalize(snapshot->hostname, hostname) || strcmp(hostname, snapshot->hostname)) return false;
    bool visible = false;
    for (const unsigned char *p = (const unsigned char *) snapshot->title; *p; ++p) {
        if (*p < 32 || *p == 127) return false;
        if (*p > 32) visible = true;
    }
    if (!visible) return false;
    if (config->jenkins_url[0]) {
        if (!app_config_jenkins_is_valid(config) || strpbrk(config->jenkins_url, "\r\n?#") ||
            (config->jenkins_job_path[0] && (strncmp(config->jenkins_job_path, "/job/", 5) ||
             strpbrk(config->jenkins_job_path, "\r\n?#")))) return false;
    } else if (config->jenkins_user[0] || config->jenkins_token[0] || config->jenkins_job_path[0]) return false;
    for (uint8_t i = 0; i < snapshot->wifi.count; ++i) {
        const app_wifi_profile_t *profile = &snapshot->wifi.entries[i];
        if (!terminated(profile->ssid, sizeof(profile->ssid)) || !profile->ssid[0] ||
            !terminated(profile->username, sizeof(profile->username)) ||
            !terminated(profile->password, sizeof(profile->password)) ||
            (unsigned) profile->auth >= APP_WIFI_AUTH_COUNT ||
            (profile->auth == APP_WIFI_AUTH_ENTERPRISE && (!profile->username[0] || !profile->password[0]))) return false;
        for (uint8_t j = 0; j < i; ++j) if (!strcmp(profile->ssid, snapshot->wifi.entries[j].ssid)) return false;
    }
    return true;
}

esp_err_t app_settings_snapshot_stage(const app_settings_snapshot_t *snapshot)
{
    if (!app_settings_snapshot_valid(snapshot) || snapshot->branding_slots[0] > 2 ||
        snapshot->branding_slots[1] > 2) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(app_config_store_staging_namespace(), NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
#define SAVE(call) do { if (err == ESP_OK) err = (call); } while (0)
    SAVE(nvs_erase_all(handle));
    SAVE(nvs_set_u8(handle, NVS_KEY_WIFI_COUNT, snapshot->wifi.count));
    SAVE(nvs_set_u8(handle, NVS_KEY_WIFI_LAST, snapshot->wifi.last_index));
    SAVE(nvs_set_u8(handle, NVS_KEY_WIFI_ORDERED, 1));
    for (uint8_t i = 0; i < snapshot->wifi.count && err == ESP_OK; ++i) {
        const app_wifi_profile_t *profile = &snapshot->wifi.entries[i];
        char key[16];
        wifi_profile_key(key, NVS_KEY_WIFI_SSID, i); SAVE(nvs_set_str(handle, key, profile->ssid));
        wifi_profile_key(key, NVS_KEY_WIFI_USERNAME, i); SAVE(nvs_set_str(handle, key, profile->username));
        wifi_profile_key(key, NVS_KEY_WIFI_PASSWORD, i); SAVE(nvs_set_str(handle, key, profile->password));
        wifi_profile_key(key, NVS_KEY_WIFI_AUTH, i); SAVE(nvs_set_u8(handle, key, profile->auth));
    }
    const app_config_t *config = &snapshot->config;
    SAVE(nvs_set_str(handle, NVS_KEY_JENKINS_URL, config->jenkins_url));
    SAVE(nvs_set_str(handle, NVS_KEY_JENKINS_JOB_PATH, config->jenkins_job_path));
    SAVE(nvs_set_str(handle, NVS_KEY_JENKINS_USER, config->jenkins_user));
    SAVE(nvs_set_str(handle, NVS_KEY_JENKINS_TOKEN, config->jenkins_token));
    SAVE(nvs_set_u32(handle, NVS_KEY_JENKINS_POLL_INTERVAL, config->jenkins_poll_interval_minutes));
    SAVE(nvs_set_u32(handle, NVS_KEY_JENKINS_LIST_TIMEOUT, config->jenkins_job_list_timeout_seconds));
    SAVE(nvs_set_u32(handle, NVS_KEY_JENKINS_STATUS_TIMEOUT, config->jenkins_status_timeout_seconds));
    SAVE(nvs_set_u8(handle, NVS_KEY_LIGHT_BRIGHTNESS, config->light_brightness_percent));
    SAVE(nvs_set_u8(handle, NVS_KEY_BUILD_EFFECT, config->build_effect));
    SAVE(nvs_set_u8(handle, NVS_KEY_CPU_MODE, config->cpu_mode));
    SAVE(nvs_set_str(handle, NVS_KEY_UI_LANGUAGE, snapshot->language));
    SAVE(nvs_set_str(handle, NVS_KEY_SITE_TITLE, snapshot->title));
    SAVE(nvs_set_str(handle, NVS_KEY_HOSTNAME, snapshot->hostname));
    SAVE(nvs_set_str(handle, NVS_KEY_ADMIN_USERNAME, snapshot->admin_username));
    SAVE(nvs_set_blob(handle, NVS_KEY_ADMIN_SALT, snapshot->admin_salt, sizeof(snapshot->admin_salt)));
    SAVE(nvs_set_blob(handle, NVS_KEY_ADMIN_HASH, snapshot->admin_hash, sizeof(snapshot->admin_hash)));
    SAVE(nvs_set_blob(handle, "lights", &snapshot->lights, sizeof(snapshot->lights)));
    SAVE(nvs_set_blob(handle, "ota_auto", &snapshot->ota, sizeof(snapshot->ota)));
    SAVE(nvs_set_blob(handle, "ota_history", &snapshot->ota_history, sizeof(snapshot->ota_history)));
    SAVE(nvs_set_u8(handle, "brand_logo", snapshot->branding_slots[0]));
    SAVE(nvs_set_u8(handle, "brand_icon", snapshot->branding_slots[1]));
    SAVE(nvs_commit(handle));
#undef SAVE
    nvs_close(handle);
    return err;
}
