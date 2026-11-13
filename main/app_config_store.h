#pragma once
#include "esp_err.h"

esp_err_t app_config_store_init(void);
const char *app_settings_namespace(void);
const char *app_config_store_staging_namespace(void);
/* Activates on the next boot; callers must stop writes and restart afterwards. */
esp_err_t app_config_store_activate(void);
esp_err_t app_config_store_factory_reset(void);
