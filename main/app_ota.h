#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

void app_ota_init(void);
/* Called after storage, LEDs and a local web server have started successfully. */
void app_ota_confirm_startup(bool web_server_ready);

esp_err_t app_ota_status_handler(httpd_req_t *request);
esp_err_t app_ota_check_handler(httpd_req_t *request);
esp_err_t app_ota_install_handler(httpd_req_t *request);
esp_err_t app_ota_script_handler(httpd_req_t *request);
