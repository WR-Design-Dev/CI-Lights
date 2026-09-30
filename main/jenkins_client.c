#include "jenkins_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "traffic_light.h"

#define RESPONSE_BUFFER_SIZE 128
#define JENKINS_REQUEST_URL_MAX_LENGTH 512
#define JENKINS_POLL_CHECK_INTERVAL_MS 60000
#define JENKINS_POLL_TASK_STACK_SIZE 20480
#define JENKINS_STARTUP_RETRY_COUNT 3
#define JENKINS_STARTUP_RETRY_DELAY_MS 5000

static const char *TAG = "jenkins";
static volatile bool s_request_in_progress;

typedef struct {
    char body[RESPONSE_BUFFER_SIZE];
    size_t length;
    bool truncated;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        http_response_t *response = event->user_data;
        if (response != NULL) {
            size_t remaining = sizeof(response->body) - response->length - 1;
            size_t bytes_to_copy = (size_t) event->data_len < remaining
                                       ? (size_t) event->data_len
                                       : remaining;

            memcpy(response->body + response->length, event->data, bytes_to_copy);
            response->length += bytes_to_copy;
            response->body[response->length] = '\0';
            response->truncated |= bytes_to_copy < (size_t) event->data_len;
        }
    }

    return ESP_OK;
}

static void log_runtime_diagnostics(void)
{
    const uint32_t internal_free = (uint32_t) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_min = (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_largest = (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_free = (uint32_t) heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    wifi_ap_record_t ap = {0};
    esp_err_t wifi_err = esp_wifi_sta_get_ap_info(&ap);

    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Diagnose: WLAN RSSI %d dBm, Kanal %u; Heap intern frei %lu B, Minimum %lu B, groesster Block %lu B; PSRAM frei %lu B",
                 ap.rssi, ap.primary, (unsigned long) internal_free,
                 (unsigned long) internal_min, (unsigned long) internal_largest,
                 (unsigned long) psram_free);
    } else {
        ESP_LOGW(TAG, "Diagnose: WLAN nicht verbunden (%s); Heap intern frei %lu B, Minimum %lu B, groesster Block %lu B; PSRAM frei %lu B",
                 esp_err_to_name(wifi_err), (unsigned long) internal_free,
                 (unsigned long) internal_min, (unsigned long) internal_largest,
                 (unsigned long) psram_free);
    }
}

static bool update_traffic_light_from_response(const http_response_t *response)
{
    if (response->truncated) {
        ESP_LOGW(TAG, "Jenkins-Antwort ist zu lang, Status kann nicht gelesen werden");
        return false;
    }

    cJSON *json = cJSON_Parse(response->body);
    if (json == NULL) {
        ESP_LOGW(TAG, "Jenkins-Antwort ist kein gueltiges JSON");
        return false;
    }

    cJSON *color = cJSON_GetObjectItemCaseSensitive(json, "color");
    if (!cJSON_IsString(color) || color->valuestring == NULL) {
        ESP_LOGW(TAG, "Jenkins-Antwort enthaelt keinen Job-Status");
        cJSON_Delete(json);
        return false;
    }

    const char *jenkins_color = color->valuestring;
    bool build_running = strstr(jenkins_color, "_anime") != NULL;
    traffic_light_color_t traffic_light_color;
    if (strncmp(jenkins_color, "blue", 4) == 0) {
        traffic_light_color = TRAFFIC_LIGHT_GREEN;
    } else if (strncmp(jenkins_color, "yellow", 6) == 0) {
        traffic_light_color = TRAFFIC_LIGHT_YELLOW;
    } else if (strncmp(jenkins_color, "red", 3) == 0) {
        traffic_light_color = TRAFFIC_LIGHT_RED;
    } else if (strncmp(jenkins_color, "grey", 4) == 0 ||
               strncmp(jenkins_color, "aborted", 7) == 0 ||
               strncmp(jenkins_color, "notbuilt", 8) == 0) {
        traffic_light_color = TRAFFIC_LIGHT_GREY;
    } else if (strncmp(jenkins_color, "disabled", 8) == 0) {
        traffic_light_color = TRAFFIC_LIGHT_OFF;
    } else {
        ESP_LOGW(TAG, "Unbekannte Jenkins-Farbe: %s", jenkins_color);
        traffic_light_color = TRAFFIC_LIGHT_YELLOW;
    }

    if (build_running && traffic_light_color != TRAFFIC_LIGHT_OFF &&
        traffic_light_color != TRAFFIC_LIGHT_GREY) {
        traffic_light_set_build_running(traffic_light_color);
    } else {
        traffic_light_set(traffic_light_color);
    }

    ESP_LOGI(TAG, "Jenkins-Farbe: %s", jenkins_color);
    cJSON_Delete(json);
    return true;
}

static bool jenkins_client_request_internal(const app_config_t *config, bool show_loading,
                                            bool *retryable)
{
    if (retryable != NULL) {
        *retryable = false;
    }
    if (traffic_light_control_mode() != APP_CONTROL_MODE_AUTO) {
        ESP_LOGI(TAG, "Jenkins-Abfrage wird durch die gewaehlte Betriebsart ausgesetzt");
        return true;
    }
    if (config == NULL || config->jenkins_url[0] == '\0' ||
        config->jenkins_user[0] == '\0' || config->jenkins_token[0] == '\0' ||
        config->jenkins_job_path[0] == '\0') {
        ESP_LOGE(TAG, "Jenkins-Konfiguration ist unvollstaendig");
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
        return false;
    }

    char request_url[JENKINS_REQUEST_URL_MAX_LENGTH];
    int length = snprintf(request_url, sizeof(request_url), "%s%s?tree=color",
                          config->jenkins_url, config->jenkins_job_path);
    if (length < 0 || length >= sizeof(request_url)) {
        ESP_LOGE(TAG, "Jenkins-URL ist zu lang");
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
        return false;
    }

    http_response_t response = {0};
    esp_http_client_config_t http_config = {
        .url = request_url,
        .username = config->jenkins_user,
        .password = config->jenkins_token,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .event_handler = http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP-Client konnte nicht initialisiert werden");
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
        return false;
    }

    if (show_loading) {
        s_request_in_progress = true;
        traffic_light_start_query_animation();
    }
    int64_t request_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Jenkins-Abfrage startet");
    esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "Jenkins-Abfrage beendet nach %lld ms: %s",
             (long long) ((esp_timer_get_time() - request_start_us) / 1000),
             esp_err_to_name(err));

    int status_code = esp_http_client_get_status_code(client);
    bool status_updated = false;
    if (err == ESP_OK && status_code >= 200 && status_code < 300) {
        ESP_LOGI(TAG, "Jenkins antwortete mit HTTP-Status %d", status_code);
        status_updated = update_traffic_light_from_response(&response);
        if (!status_updated) {
            traffic_light_set(TRAFFIC_LIGHT_RED);
            traffic_light_start_error_sos_animation();
        }
    } else {
        ESP_LOGE(TAG, "Jenkins-Anfrage fehlgeschlagen: %s (HTTP %d)",
                 esp_err_to_name(err), status_code);
        traffic_light_set(TRAFFIC_LIGHT_RED);
        traffic_light_start_error_sos_animation();
    }
    if (!status_updated && retryable != NULL) {
        *retryable = (err != ESP_OK && err != ESP_ERR_NO_MEM) ||
                     status_code == 429 || status_code >= 500 ||
                     (err == ESP_OK && status_code >= 200 && status_code < 300);
    }
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Jenkins-Abfrage: Stackreserve der Task %lu B",
             (unsigned long) uxTaskGetStackHighWaterMark(NULL));
    if (show_loading) {
        s_request_in_progress = false;
    }
    return status_updated;
}

void jenkins_client_request(const app_config_t *config)
{
    jenkins_client_request_internal(config, true, NULL);
}

static void jenkins_client_request_without_loading(const app_config_t *config)
{
    jenkins_client_request_internal(config, false, NULL);
}

bool jenkins_client_request_in_progress(void)
{
    return s_request_in_progress;
}

void jenkins_client_set_control_mode(app_control_mode_t mode)
{
    traffic_light_set_control_mode(mode);
    if (mode != APP_CONTROL_MODE_AUTO) {
        ESP_LOGI(TAG, "Betriebsart ausserhalb von Jenkins aktiviert");
        return;
    }

    ESP_LOGI(TAG, "Jenkins-Betriebsart aktiviert; aktualisiere Jenkins-Status");
    app_config_t config = {0};
    if (app_config_load(&config) && config.jenkins_url[0] != '\0' &&
        config.jenkins_job_path[0] != '\0') {
        jenkins_client_request(&config);
    }
}

static void jenkins_poll_task(void *argument)
{
    (void) argument;

    log_runtime_diagnostics();

    /* Perform the first HTTPS request in the polling task rather than app_main. */
    app_config_t initial_config = {0};
    if (app_config_load(&initial_config) && initial_config.jenkins_url[0] != '\0' &&
        initial_config.jenkins_job_path[0] != '\0') {
        ESP_LOGI(TAG, "Frage Jenkins direkt nach dem Start ab");
        for (unsigned attempt = 0; attempt < JENKINS_STARTUP_RETRY_COUNT; ++attempt) {
            bool retryable = false;
            if (jenkins_client_request_internal(&initial_config, true, &retryable) ||
                !retryable) {
                break;
            }
            if (attempt + 1 == JENKINS_STARTUP_RETRY_COUNT) {
                break;
            }
            ESP_LOGW(TAG, "Startabfrage fehlgeschlagen; neuer Versuch in %u ms (%u/%u)",
                     JENKINS_STARTUP_RETRY_DELAY_MS, attempt + 2,
                     JENKINS_STARTUP_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(JENKINS_STARTUP_RETRY_DELAY_MS));
            if (traffic_light_control_mode() != APP_CONTROL_MODE_AUTO ||
                !app_config_load(&initial_config) || initial_config.jenkins_url[0] == '\0' ||
                initial_config.jenkins_job_path[0] == '\0') {
                break;
            }
        }
    }

    int64_t last_poll_time_us = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(JENKINS_POLL_CHECK_INTERVAL_MS));
        log_runtime_diagnostics();

        app_config_t config = {0};
        if (!app_config_load(&config) || config.jenkins_url[0] == '\0' ||
            config.jenkins_job_path[0] == '\0') {
            continue;
        }

        uint32_t poll_interval_minutes = config.jenkins_poll_interval_minutes;
        if (poll_interval_minutes < 1) {
            poll_interval_minutes = APP_JENKINS_POLL_INTERVAL_DEFAULT_MINUTES;
        }
        int64_t poll_interval_us = (int64_t) poll_interval_minutes * 60 * 1000000;
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_poll_time_us >= poll_interval_us) {
            ESP_LOGI(TAG, "Frage Jenkins nach %lu Minuten erneut ab",
                     (unsigned long) poll_interval_minutes);
            jenkins_client_request_without_loading(&config);
            last_poll_time_us = now_us;
        }
    }
}

void jenkins_client_start_polling(void)
{
    if (xTaskCreate(jenkins_poll_task, "jenkins_poll", JENKINS_POLL_TASK_STACK_SIZE,
                    NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Jenkins-Abfrage-Task konnte nicht gestartet werden");
    }
}
