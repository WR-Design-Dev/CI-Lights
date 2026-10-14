#include "app_security.h"

#include <string.h>
#include <stdlib.h>
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_secure_boot.h"
#include "sdkconfig.h"

#if CONFIG_APP_SECURITY_SECURE
#include "app_secure_keys.h"
#if !CONFIG_SECURE_BOOT_V2_ENABLED || !CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE || !CONFIG_SECURE_DISABLE_ROM_DL_MODE || !CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC
#error "Secure profile requires Secure Boot v2, release flash encryption, encrypted NVS and permanently disabled ROM download"
#endif
_Static_assert((CONFIG_APP_SECURE_REVOKE_MASK & (1 << CONFIG_APP_SECURE_KEY_INDEX)) == 0,
               "The active signing key must never be revoked");
#endif

static bool s_ready;
static const char *s_rotation = "none";
static const char *TAG = "security";

const char *app_security_profile(void)
{
    return CONFIG_APP_SECURITY_SECURE ? "secure" : "standard";
}

const char *app_security_layout(void)
{
    return CONFIG_APP_SECURITY_SECURE ? "4mb-secure-v1" : "4mb-ota-v1";
}

const char *app_security_project(void)
{
    return CONFIG_APP_SECURITY_SECURE ? "CI-Lights-Secure" : "CI-Lights";
}

bool app_security_ready(void) { return s_ready; }

static unsigned revoked_mask(void)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (esp_efuse_get_digest_revoke(i)) mask |= 1u << i;
    }
    return mask;
}

#if CONFIG_APP_SECURITY_SECURE
static bool protected_flash_key(esp_efuse_purpose_t purpose)
{
    esp_efuse_block_t block;
    return esp_efuse_find_purpose(purpose, &block) && block != EFUSE_BLK_KEY5 &&
           esp_efuse_get_key_dis_read(block) && esp_efuse_get_key_dis_write(block) &&
           esp_efuse_get_keypurpose_dis_write(block);
}

static bool expected_digests(void)
{
    for (unsigned i = 0; i < 3; ++i) {
        esp_efuse_block_t block;
        uint8_t digest[32];
        if (!esp_efuse_find_purpose((esp_efuse_purpose_t) (ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST0 + i), &block) ||
            esp_efuse_get_key_dis_read(block) || !esp_efuse_get_key_dis_write(block) ||
            !esp_efuse_get_keypurpose_dis_write(block) ||
            esp_efuse_read_field_blob(esp_efuse_get_key(block), digest, 256) != ESP_OK ||
            memcmp(digest, app_secure_digests[i], sizeof(digest)) != 0) return false;
    }
    return !esp_efuse_get_digest_revoke(CONFIG_APP_SECURE_KEY_INDEX);
}

static bool verify_partition_with_active_key(const esp_partition_t *partition)
{
    if (partition == NULL) return false;
    esp_partition_pos_t position = {.offset = partition->address, .size = partition->size};
    return esp_secure_boot_verify_with_efuse_digest_index(CONFIG_APP_SECURE_KEY_INDEX, &position) == ESP_OK;
}
#endif

bool app_security_init(void)
{
#if CONFIG_APP_SECURITY_SECURE
    const esp_partition_t *keys = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                          ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    const esp_partition_t *branding = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                              (esp_partition_subtype_t) 0x40, "branding");
    s_ready = esp_secure_boot_enabled() && esp_efuse_is_flash_encryption_enabled() &&
              esp_flash_encryption_cfg_verify_release_mode() &&
              esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE) &&
              esp_efuse_read_field_bit(ESP_EFUSE_DIS_PAD_JTAG) &&
              esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_JTAG) &&
              expected_digests() &&
              protected_flash_key(ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1) &&
              protected_flash_key(ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_2) &&
              keys != NULL && keys->encrypted && keys->size == 0x1000 &&
              branding != NULL && branding->encrypted;
    if (CONFIG_APP_SECURE_REVOKE_MASK != 0) s_rotation = "pending";
#else
    s_ready = !esp_secure_boot_enabled() && !esp_efuse_is_flash_encryption_enabled();
#endif
    if (!s_ready) ESP_LOGE(TAG, "Sicherheitszustand passt nicht zum Firmware-Profil; Einrichtung gesperrt");
    return s_ready;
}

esp_err_t app_security_apply_key_policy(void)
{
#if CONFIG_APP_SECURITY_SECURE
    unsigned requested = CONFIG_APP_SECURE_REVOKE_MASK;
    if (requested == 0) return ESP_OK;
    if (!s_ready || (requested & (1u << CONFIG_APP_SECURE_KEY_INDEX))) return ESP_ERR_INVALID_STATE;
    if ((revoked_mask() & requested) == requested) {
        s_rotation = "complete";
        return ESP_OK;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *fallback = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t running_state, fallback_state;
    /* Two successful releases with the new key establish a tested fallback.
       A reset before revocation leaves all prior trust intact. */
    if (esp_ota_get_state_partition(running, &running_state) != ESP_OK || running_state != ESP_OTA_IMG_VALID ||
        fallback == running || esp_ota_get_state_partition(fallback, &fallback_state) != ESP_OK ||
        fallback_state != ESP_OTA_IMG_VALID || !verify_partition_with_active_key(running) ||
        !verify_partition_with_active_key(fallback)) {
        s_rotation = "waiting_for_signed_fallback";
        return ESP_OK;
    }
    esp_partition_pos_t bootloader = {.offset = CONFIG_BOOTLOADER_OFFSET_IN_FLASH,
                                     .size = CONFIG_PARTITION_TABLE_OFFSET - CONFIG_BOOTLOADER_OFFSET_IN_FLASH};
    if (esp_secure_boot_verify_with_efuse_digest_index(CONFIG_APP_SECURE_KEY_INDEX, &bootloader) != ESP_OK) {
        s_rotation = "bootloader_verification_failed";
        return ESP_ERR_INVALID_STATE;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if ((requested & (1u << i)) && !esp_efuse_get_digest_revoke(i)) {
            esp_err_t err = esp_ota_revoke_secure_boot_public_key((esp_ota_secure_boot_public_key_index_t) i);
            if (err != ESP_OK) {
                s_rotation = "revocation_failed";
                return err;
            }
        }
    }
    s_rotation = (revoked_mask() & requested) == requested ? "complete" : "revocation_failed";
    return strcmp(s_rotation, "complete") == 0 ? ESP_OK : ESP_FAIL;
#else
    return ESP_OK;
#endif
}

cJSON *app_security_status_json(void)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) return NULL;
    if (cJSON_AddStringToObject(json, "profile", app_security_profile()) == NULL ||
        cJSON_AddBoolToObject(json, "ready", s_ready) == NULL ||
        cJSON_AddBoolToObject(json, "secure_boot", esp_secure_boot_enabled()) == NULL ||
        cJSON_AddBoolToObject(json, "flash_encryption", esp_efuse_is_flash_encryption_enabled()) == NULL ||
        cJSON_AddBoolToObject(json, "rom_download_disabled", esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)) == NULL ||
        cJSON_AddNumberToObject(json, "revoked_keys", revoked_mask()) == NULL ||
        cJSON_AddStringToObject(json, "key_rotation", s_rotation) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
#if CONFIG_APP_SECURITY_SECURE
    cJSON_AddNumberToObject(json, "signing_key", CONFIG_APP_SECURE_KEY_INDEX);
    cJSON_AddNumberToObject(json, "revocation_policy", CONFIG_APP_SECURE_REVOKE_MASK);
    cJSON_AddBoolToObject(json, "nvs_encryption", true);
#else
    cJSON_AddBoolToObject(json, "nvs_encryption", false);
#endif
    return json;
}

esp_err_t app_security_status_handler(httpd_req_t *request)
{
    cJSON *json = app_security_status_json();
    if (json == NULL) return ESP_ERR_NO_MEM;
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, body);
    free(body);
    return err;
}

extern const unsigned char security_js_start[] asm("_binary_security_js_start");
extern const unsigned char security_js_end[] asm("_binary_security_js_end");

esp_err_t app_security_script_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/javascript; charset=utf-8");
    return httpd_resp_send(request, (const char *) security_js_start, security_js_end - security_js_start - 1);
}
