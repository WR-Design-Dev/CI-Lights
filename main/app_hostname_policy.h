#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_HOSTNAME_MAX_LENGTH 32
#define APP_HOSTNAME_ACTIVE_MAX_LENGTH 63
#define APP_HOSTNAME_DEFAULT_SIZE 19

typedef struct {
    char configured[APP_HOSTNAME_MAX_LENGTH + 1];
    char active[APP_HOSTNAME_ACTIVE_MAX_LENGTH + 1];
    char default_name[APP_HOSTNAME_DEFAULT_SIZE];
    bool fallback;
} app_hostname_policy_t;

bool app_hostname_normalize(const char *input, char output[APP_HOSTNAME_MAX_LENGTH + 1]);
void app_hostname_policy_init(app_hostname_policy_t *policy, const char *configured, const uint8_t mac[6]);
/* Returns true exactly once when a custom name is changed by collision handling. */
bool app_hostname_policy_observe(app_hostname_policy_t *policy, const char *active);
bool app_hostname_policy_using_default(const app_hostname_policy_t *policy);
