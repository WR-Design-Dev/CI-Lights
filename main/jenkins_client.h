#pragma once

#include "app_config.h"

void jenkins_client_request(const app_config_t *config);
bool jenkins_client_request_in_progress(void);
void jenkins_client_set_control_mode(app_control_mode_t mode);
void jenkins_client_start_polling(void);
