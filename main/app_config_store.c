#include "app_config_store.h"
#include "app_branding.h"
#include "nvs.h"

static uint8_t s_bank;
static const char *const BANKS[] = {"settings", "settings_b"};

esp_err_t app_config_store_init(void)
{
    nvs_handle_t handle;
    s_bank = 0;
    esp_err_t err = nvs_open("cfg_meta", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_get_u8(handle, "active", &s_bank);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { s_bank = 0; return ESP_OK; }
    return err != ESP_OK ? err : s_bank > 1 ? ESP_ERR_INVALID_STATE : ESP_OK;
}

const char *app_settings_namespace(void) { return BANKS[s_bank]; }
const char *app_config_store_staging_namespace(void) { return BANKS[1 - s_bank]; }

esp_err_t app_config_store_activate(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("cfg_meta", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "active", 1 - s_bank);
    if (err == ESP_OK) err = nvs_commit(handle);
    /* NVS setters can already have published the atomic value when a later
       commit reports failure. Never continue writing through the old bank in
       that case: complete the restart into the fully prepared bank instead. */
    if (err != ESP_OK) {
        uint8_t selected;
        if (nvs_get_u8(handle, "active", &selected) == ESP_OK && selected == 1 - s_bank) err = ESP_OK;
    }
    nvs_close(handle);
    return err;
}

esp_err_t app_config_store_factory_reset(void)
{
    /* Retain the currently selected branding, never an uncommitted staging image. */
    uint8_t slots[2];
    app_branding_active_slots(slots);
    esp_err_t err = ESP_OK;
    nvs_handle_t handle;
    for (unsigned bank = 0; bank < 2 && err == ESP_OK; ++bank) {
        err = nvs_open(BANKS[bank], NVS_READWRITE, &handle);
        if (err != ESP_OK) break;
        err = nvs_erase_all(handle);
        if (err == ESP_OK) err = nvs_set_u8(handle, "brand_logo", slots[0]);
        if (err == ESP_OK) err = nvs_set_u8(handle, "brand_icon", slots[1]);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    /* Keep the selector: both banks now contain only the same retained branding. */
    return err;
}
