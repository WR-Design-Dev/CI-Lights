#include "app_hostname_policy.h"
#include <stdio.h>
#include <string.h>

bool app_hostname_normalize(const char *input, char output[APP_HOSTNAME_MAX_LENGTH + 1])
{
    if (input == NULL || output == NULL) return false;
    size_t length = strlen(input);
    if (length > APP_HOSTNAME_MAX_LENGTH || (length && (input[0] == '-' || input[length - 1] == '-'))) return false;
    for (size_t i = 0; i < length; ++i) {
        char ch = input[i];
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    }
    for (size_t i = 0; i <= length; ++i) {
        char ch = input[i];
        output[i] = ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
    }
    return true;
}

void app_hostname_policy_init(app_hostname_policy_t *policy, const char *configured, const uint8_t mac[6])
{
    memset(policy, 0, sizeof(*policy));
    snprintf(policy->default_name, sizeof(policy->default_name), "ci-lights-%02x%02x", mac[4], mac[5]);
    if (!app_hostname_normalize(configured, policy->configured)) policy->configured[0] = '\0';
    snprintf(policy->active, sizeof(policy->active), "%s", policy->configured[0] ? policy->configured : policy->default_name);
}

bool app_hostname_policy_observe(app_hostname_policy_t *policy, const char *active)
{
    if (active == NULL || active[0] == '\0') return false;
    snprintf(policy->active, sizeof(policy->active), "%s", active);
    if (policy->configured[0] && !policy->fallback && strcmp(policy->configured, policy->active) != 0) {
        policy->fallback = true;
        return true;
    }
    return false;
}

bool app_hostname_policy_using_default(const app_hostname_policy_t *policy)
{
    size_t length = strlen(policy->default_name);
    if (strncmp(policy->active, policy->default_name, length) != 0) return false;
    const char *suffix = policy->active + length;
    if (*suffix == '\0') return true;
    if (*suffix++ != '-' || *suffix == '\0') return false;
    while (*suffix) {
        char ch = *suffix++;
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}
