#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "sdkconfig.h"
#ifndef CONFIG_APP_SECURITY_SECURE
#define CONFIG_APP_SECURITY_SECURE 0
#endif

bool app_security_init(void);
bool app_security_ready(void);
const char *app_security_profile(void);
const char *app_security_layout(void);
const char *app_security_project(void);
/* Called only after the running image has passed startup and rollback checks. */
esp_err_t app_security_apply_key_policy(void);
cJSON *app_security_status_json(void);
esp_err_t app_security_status_handler(httpd_req_t *request);
esp_err_t app_security_script_handler(httpd_req_t *request);
