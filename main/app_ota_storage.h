#pragma once
#include "app_ota_policy.h"
#include "esp_err.h"

esp_err_t app_ota_storage_load(app_ota_settings_t *settings, app_ota_history_t *history);
esp_err_t app_ota_storage_save_settings(const app_ota_settings_t *settings, const app_ota_history_t *history);
esp_err_t app_ota_storage_save_history(const app_ota_history_t *history);
