#pragma once
#include "app_hostname_policy.h"
#include "esp_http_server.h"

esp_err_t app_hostname_start(void);
void app_hostname_stop(void);
void app_hostname_snapshot(app_hostname_policy_t *policy);
esp_err_t app_hostname_get_handler(httpd_req_t *request);
esp_err_t app_hostname_post_handler(httpd_req_t *request);
esp_err_t app_hostname_script_handler(httpd_req_t *request);
