#pragma once

#include <stdbool.h>

#include "app_config.h"
#include "esp_err.h"

esp_err_t app_cpu_init(void);
esp_err_t app_cpu_apply_mode(app_cpu_mode_t mode);
app_cpu_mode_t app_cpu_current_mode(void);
const char *app_cpu_mode_name(app_cpu_mode_t mode);
bool app_cpu_mode_from_name(const char *name, app_cpu_mode_t *mode);
bool app_cpu_boost_begin(void);
void app_cpu_boost_end(bool boosted);
