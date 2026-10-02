/* Retain the connection address when other components compile warnings only. */
#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include "app_config.h"
#include "app_cpu.h"

#include <string.h>

#include "esp_event.h"
#include "esp_eap_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_STOPPING_BIT BIT2
#define WIFI_MAXIMUM_RETRIES 5
#define WIFI_CONNECTION_TIMEOUT_MS 30000
#define WIFI_RETRY_PAUSE_MS 2000

static const char *TAG = "network";
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_sta_netif;
static int s_wifi_retry_count;
static bool s_wifi_enterprise_enabled;
static bool s_wifi_had_ip;
static bool s_wifi_reconnecting;
static uint8_t s_wifi_last_disconnect_reason;

static const char *wifi_disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "Authentifizierung abgelaufen";
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "802.1X-/EAP-Authentifizierung fehlgeschlagen";
    case WIFI_REASON_NO_AP_FOUND:
        return "Access Point nicht gefunden";
    case WIFI_REASON_AUTH_FAIL:
        return "Authentifizierung fehlgeschlagen";
    case WIFI_REASON_ASSOC_FAIL:
        return "Anmeldung beim Access Point fehlgeschlagen";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Authentifizierungs-Handshake Zeitueberschreitung";
    case WIFI_REASON_CONNECTION_FAIL:
        return "Verbindungsaufbau fehlgeschlagen";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Access Point antwortet nicht mehr";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "Kein Access Point mit passender Verschluesselung gefunden";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "Kein Access Point mit passendem Sicherheitsmodus gefunden";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "Signal des Access Points ist zu schwach";
    default:
        return "unbekannt";
    }
}

void app_network_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_STOPPING_BIT) {
            return;
        }
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Erster WLAN-Verbindungsversuch konnte nicht gestartet werden: %s",
                     esp_err_to_name(err));
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_STOPPING_BIT) {
            return;
        }
        const wifi_event_sta_disconnected_t *disconnection = event_data;
        if (disconnection != NULL) {
            s_wifi_last_disconnect_reason = disconnection->reason;
        }
        if (s_wifi_had_ip) {
            if (!s_wifi_reconnecting) {
                ESP_LOGW(TAG, "WLAN-Verbindung verloren: Grund %u (%s); verbinde erneut",
                         s_wifi_last_disconnect_reason,
                         wifi_disconnect_reason_name(s_wifi_last_disconnect_reason));
                s_wifi_reconnecting = true;
            }
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "WLAN-Wiederverbindung konnte nicht gestartet werden: %s",
                         esp_err_to_name(err));
            }
        } else if (s_wifi_retry_count < WIFI_MAXIMUM_RETRIES) {
            esp_err_t err = esp_wifi_connect();
            if (err == ESP_OK) {
                s_wifi_retry_count++;
            } else {
                ESP_LOGW(TAG, "WLAN-Verbindungsversuch konnte nicht gestartet werden: %s",
                         esp_err_to_name(err));
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
            }
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_STOPPING_BIT) {
            return;
        }
        const ip_event_got_ip_t *got_ip = event_data;
        if (got_ip != NULL) {
            ESP_LOGI(TAG, "WLAN verbunden: IP " IPSTR, IP2STR(&got_ip->ip_info.ip));
        }
        esp_err_t ipv6_err = esp_netif_create_ip6_linklocal(s_wifi_sta_netif);
        if (ipv6_err != ESP_OK) {
            ESP_LOGW(TAG, "IPv6-Link-Local-Adresse konnte nicht erstellt werden: %s",
                     esp_err_to_name(ipv6_err));
        }
        s_wifi_retry_count = 0;
        s_wifi_had_ip = true;
        s_wifi_reconnecting = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool configure_enterprise_wifi(const app_config_t *config)
{
    size_t username_length = strlen(config->wifi_username);
    size_t password_length = strlen(config->wifi_password);
    if (username_length == 0 || password_length == 0) {
        ESP_LOGE(TAG, "WPA2-Enterprise braucht Benutzername und Passwort");
        return false;
    }

    /* The user explicitly requested an Enterprise connection without a CA
     * certificate. This accepts any server certificate and is unsafe. */
    esp_err_t err = esp_eap_client_use_default_cert_bundle(false);
    if (err == ESP_OK) {
        err = esp_eap_client_set_identity((const unsigned char *) config->wifi_username,
                                          username_length);
    }
    if (err == ESP_OK) {
        err = esp_eap_client_set_username((const unsigned char *) config->wifi_username,
                                          username_length);
    }
    if (err == ESP_OK) {
        err = esp_eap_client_set_password((const unsigned char *) config->wifi_password,
                                          password_length);
    }
    if (err == ESP_OK) {
        err = esp_eap_client_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_MSCHAPV2);
    }
    if (err == ESP_OK) {
        err = esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP | ESP_EAP_TYPE_TTLS);
    }
    if (err == ESP_OK) {
        err = esp_wifi_sta_enterprise_enable();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WPA2-Enterprise konnte nicht eingerichtet werden: %s",
                 esp_err_to_name(err));
        esp_eap_client_clear_identity();
        esp_eap_client_clear_username();
        esp_eap_client_clear_password();
        return false;
    }

    s_wifi_enterprise_enabled = true;
    ESP_LOGW(TAG, "WPA2-Enterprise aktiviert: PEAP und TTLS/MSCHAPv2 erlaubt; "
             "Server-Zertifikatspruefung ist deaktiviert");
    return true;
}

bool app_connect_to_wifi(const app_config_t *config, uint32_t timeout_ms)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Kein Speicher fuer Wi-Fi-Ereignisse");
        return false;
    }

    s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_sta_netif == NULL) {
        ESP_LOGE(TAG, "Kein Speicher fuer Wi-Fi-Station-Netzwerkinterface");
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return false;
    }
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    bool use_enterprise = config->wifi_username[0] != '\0';
    if (use_enterprise) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
        wifi_config.sta.pmf_cfg.capable = true;
    } else {
        memcpy(wifi_config.sta.password, config->wifi_password,
               strlen(config->wifi_password));
        /* With no password, also permit Enhanced Open (OWE). OWE has to be
         * enabled per station even when support is compiled into ESP-IDF.
         * The OPEN threshold also allows plain open and OWE transition APs. */
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        wifi_config.sta.owe_enabled = config->wifi_password[0] == '\0';
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    if (use_enterprise && !configure_enterprise_wifi(config)) {
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    app_cpu_mode_t cpu_mode = app_cpu_current_mode();
    bool automatic_cpu = cpu_mode == APP_CPU_MODE_AUTO_160 || cpu_mode == APP_CPU_MODE_AUTO_240;
    ESP_ERROR_CHECK(esp_wifi_set_ps(automatic_cpu ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));

    TickType_t started = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms == 0 ?
                                             WIFI_CONNECTION_TIMEOUT_MS : timeout_ms);
    bool connected = false;
    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout_ticks) {
            break;
        }
        TickType_t remaining = timeout_ticks - elapsed;
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                               pdFALSE, pdFALSE, remaining);
        if (bits & WIFI_CONNECTED_BIT) {
            connected = true;
            break;
        }
        if ((bits & WIFI_FAILED_BIT) == 0) {
            break;
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAILED_BIT);
        elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout_ticks) {
            break;
        }
        remaining = timeout_ticks - elapsed;
        TickType_t pause = pdMS_TO_TICKS(WIFI_RETRY_PAUSE_MS);
        if (remaining <= pause) {
            break;
        }
        vTaskDelay(pause);
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) {
            connected = true;
            break;
        }
        s_wifi_retry_count = 0;
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WLAN-Neuversuch konnte nicht gestartet werden: %s",
                     esp_err_to_name(err));
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    }
    if (!connected && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)) {
        connected = true;
    }
    if (!connected) {
        unsigned long elapsed_ms = timeout_ms == 0 ? WIFI_CONNECTION_TIMEOUT_MS : timeout_ms;
        wifi_ap_record_t connected_ap;
        if (esp_wifi_sta_get_ap_info(&connected_ap) == ESP_OK) {
            ESP_LOGW(TAG, "WLAN '%s': keine DHCP-IP nach %lu ms", config->wifi_ssid, elapsed_ms);
        } else {
            ESP_LOGW(TAG, "WLAN '%s' nach %lu ms nicht verbunden: Grund %u (%s)",
                     config->wifi_ssid, elapsed_ms, s_wifi_last_disconnect_reason,
                     wifi_disconnect_reason_name(s_wifi_last_disconnect_reason));
            /* The caller stops Wi-Fi before trying another profile. */
            xEventGroupSetBits(s_wifi_event_group, WIFI_STOPPING_BIT);
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        connected = (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
    }
    return connected;
}

void app_stop_wifi(void)
{
    if (s_wifi_event_group == NULL) {
        return;
    }

    if (s_wifi_enterprise_enabled) {
        ESP_ERROR_CHECK(esp_wifi_sta_enterprise_disable());
        s_wifi_enterprise_enabled = false;
    }
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                 &wifi_event_handler));
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_ERROR_CHECK(stop_err);
    }
    ESP_ERROR_CHECK(esp_wifi_deinit());
    esp_netif_destroy_default_wifi(s_wifi_sta_netif);
    s_wifi_sta_netif = NULL;
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
    s_wifi_retry_count = 0;
    s_wifi_had_ip = false;
    s_wifi_reconnecting = false;
    s_wifi_last_disconnect_reason = 0;
}
