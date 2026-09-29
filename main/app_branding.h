#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_BRANDING_MAX_BYTES (0x8000 - 20)

typedef enum {
    APP_BRANDING_LOGO,
    APP_BRANDING_FAVICON,
} app_branding_kind_t;

typedef enum {
    APP_BRANDING_SVG = 1,
    APP_BRANDING_PNG = 2,
    APP_BRANDING_ICO = 3,
} app_branding_format_t;

typedef struct {
    size_t length;
    app_branding_format_t format;
} app_branding_info_t;

bool app_branding_get_info(app_branding_kind_t kind, app_branding_info_t *info);
esp_err_t app_branding_read(app_branding_kind_t kind, void *buffer, size_t length);
esp_err_t app_branding_save(app_branding_kind_t kind, app_branding_format_t format,
                            const void *data, size_t length);
