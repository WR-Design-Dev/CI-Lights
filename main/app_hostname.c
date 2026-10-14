#include "app_hostname.h"
#include "app_settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include <stdlib.h>
#include <string.h>

#define HOSTNAME_FALLBACK 1u
#define HOSTNAME_STOP 2u
static const char *TAG = "hostname";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_hostname_policy_t s_policy;
static TaskHandle_t s_worker;
static SemaphoreHandle_t s_worker_done;
static bool s_stopping;
static bool s_fallback_pending;

void app_hostname_snapshot(app_hostname_policy_t *policy)
{
    portENTER_CRITICAL(&s_lock);
    *policy = s_policy;
    portEXIT_CRITICAL(&s_lock);
}

static void hostname_changed(const char *hostname, void *context)
{
    (void) context;
    TaskHandle_t notify = NULL;
    /* The callback runs inside mDNS. It must never invoke another mDNS API. */
    portENTER_CRITICAL(&s_lock);
    if (!s_stopping) {
        if (app_hostname_policy_observe(&s_policy, hostname)) {
            s_fallback_pending = true;
            notify = s_worker;
        }
        if (s_policy.fallback && app_hostname_policy_using_default(&s_policy)) s_fallback_pending = false;
    }
    portEXIT_CRITICAL(&s_lock);
    if (notify != NULL) xTaskNotify(notify, HOSTNAME_FALLBACK, eSetBits);
}

static void hostname_worker(void *context)
{
    (void) context;
    bool warned = false;
    for (;;) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
        if (events & HOSTNAME_STOP) break;
        portENTER_CRITICAL(&s_lock);
        bool pending = s_fallback_pending && !s_stopping;
        portEXIT_CRITICAL(&s_lock);
        if (!pending) continue;
        app_hostname_policy_t policy;
        app_hostname_snapshot(&policy);
        esp_err_t err = mdns_hostname_set(policy.default_name);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Wunschname belegt; verwende MAC-Standardnamen %s.local", policy.default_name);
        } else {
            if (!warned) ESP_LOGW(TAG, "MAC-Rueckfall wird erneut versucht: %s", esp_err_to_name(err));
            warned = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            xTaskNotify(s_worker, HOSTNAME_FALLBACK, eSetBits);
        }
    }
    xSemaphoreGive(s_worker_done);
    vTaskDelete(NULL);
}

void app_hostname_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    s_stopping = true;
    TaskHandle_t worker = s_worker;
    portEXIT_CRITICAL(&s_lock);
    if (worker != NULL) {
        /* Taking mDNS's service lock waits for any earlier callback notification
           to finish before the worker can exit. New callbacks see s_stopping. */
        char ignored[MDNS_NAME_BUF_LEN];
        (void) mdns_hostname_get(ignored);
        xTaskNotify(worker, HOSTNAME_STOP, eSetBits);
        xSemaphoreTake(s_worker_done, portMAX_DELAY);
    }
    s_worker = NULL;
    if (s_worker_done != NULL) vSemaphoreDelete(s_worker_done);
    s_worker_done = NULL;
    mdns_free();
}

esp_err_t app_hostname_start(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    app_hostname_policy_init(&s_policy, app_config_get_hostname(), mac);
    s_stopping = false;
    s_fallback_pending = false;
    err = mdns_init();
    if (err != ESP_OK) return err;
    s_worker_done = xSemaphoreCreateBinary();
    if (s_worker_done == NULL || xTaskCreate(hostname_worker, "hostname", 3072, NULL, 4, &s_worker) != pdPASS) {
        app_hostname_stop();
        return ESP_ERR_NO_MEM;
    }
    if ((err = mdns_register_hostname_changed_callback(hostname_changed, NULL)) != ESP_OK ||
        (err = mdns_hostname_set(s_policy.active)) != ESP_OK ||
        (err = mdns_instance_name_set("CI-Lights")) != ESP_OK) {
        app_hostname_stop();
    }
    return err;
}

esp_err_t app_hostname_get_handler(httpd_req_t *request)
{
    app_hostname_policy_t policy;
    app_hostname_snapshot(&policy);
    const char *configured = app_config_get_hostname();
    portENTER_CRITICAL(&s_lock);
    bool pending = s_fallback_pending;
    portEXIT_CRITICAL(&s_lock);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || cJSON_AddStringToObject(json, "configured", configured) == NULL ||
        cJSON_AddStringToObject(json, "active", policy.active) == NULL ||
        cJSON_AddStringToObject(json, "default_name", policy.default_name) == NULL ||
        cJSON_AddBoolToObject(json, "fallback", policy.fallback) == NULL ||
        cJSON_AddBoolToObject(json, "fallback_pending", pending) == NULL ||
        cJSON_AddBoolToObject(json, "restart_required", strcmp(configured, policy.configured) != 0) == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, body);
    free(body);
    return err;
}

esp_err_t app_hostname_post_handler(httpd_req_t *request)
{
    char body[160];
    if (request->content_len <= 0 || request->content_len >= sizeof(body))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Ungültiger Gerätename.");
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Gerätename konnte nicht gelesen werden.");
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *hostname = cJSON_GetObjectItemCaseSensitive(json, "hostname");
    esp_err_t err = cJSON_IsString(hostname) ? app_config_save_hostname(hostname->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (err != ESP_OK) return httpd_resp_send_err(request,
        err == ESP_ERR_INVALID_ARG ? HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR,
        err == ESP_ERR_INVALID_ARG ? "Ungültiger Gerätename." : "Gerätename konnte nicht gespeichert werden.");
    return app_hostname_get_handler(request);
}

extern const unsigned char hostname_js_start[] asm("_binary_hostname_js_start");
extern const unsigned char hostname_js_end[] asm("_binary_hostname_js_end");
esp_err_t app_hostname_script_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/javascript; charset=utf-8");
    return httpd_resp_send(request, (const char *) hostname_js_start, hostname_js_end - hostname_js_start - 1);
}
