#pragma once

#include "esp_http_server.h"

esp_err_t app_web_logo_handler(httpd_req_t *request);
esp_err_t app_web_traffic_light_handler(httpd_req_t *request);
esp_err_t app_web_favicon_handler(httpd_req_t *request);
esp_err_t app_web_localization_handler(httpd_req_t *request);
esp_err_t app_web_wifi_handler(httpd_req_t *request);
esp_err_t app_web_branding_status_handler(httpd_req_t *request);
esp_err_t app_web_branding_upload_handler(httpd_req_t *request);
