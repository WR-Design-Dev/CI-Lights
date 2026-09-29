#include "app_config.h"

#include <stdlib.h>
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
#define WIFI_MAXIMUM_RETRIES 5
#define WIFI_CONNECTION_TIMEOUT_MS 30000
#define WIFI_RETRY_PAUSE_MS 2000

static const char *TAG = "network";
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_sta_netif;
static int s_wifi_retry_count;
static bool s_wifi_enterprise_enabled;
static bool s_wifi_had_ip;

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

static const char *wifi_authmode_name(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "offen";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2-PSK";
    case WIFI_AUTH_ENTERPRISE:
        return "WPA2-Enterprise (802.1X)";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI-PSK";
    case WIFI_AUTH_OWE:
        return "OWE (Enhanced Open)";
    case WIFI_AUTH_WPA3_ENT_192:
        return "WPA3-Enterprise 192 Bit";
    case WIFI_AUTH_DPP:
        return "DPP";
    case WIFI_AUTH_WPA3_ENTERPRISE:
        return "WPA3-Enterprise";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return "WPA2/WPA3-Enterprise";
    case WIFI_AUTH_WPA_ENTERPRISE:
        return "WPA-Enterprise";
    case WIFI_AUTH_UNKNOWN:
        return "unbekannt oder ungueltig";
    default:
        return "unbekannt";
    }
}

static const char *wifi_cipher_name(wifi_cipher_type_t cipher)
{
    switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        return "keiner";
    case WIFI_CIPHER_TYPE_WEP40:
        return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104:
        return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP:
        return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP:
        return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        return "TKIP/CCMP";
    case WIFI_CIPHER_TYPE_GCMP:
        return "GCMP";
    case WIFI_CIPHER_TYPE_GCMP256:
        return "GCMP256";
    case WIFI_CIPHER_TYPE_UNKNOWN:
        return "unbekannt";
    default:
        return "sonstiger";
    }
}

static void log_target_wifi_security(const char *target_ssid)
{
    uint8_t ssid[APP_WIFI_SSID_MAX_LENGTH + 1] = {0};
    size_t ssid_length = strlen(target_ssid);
    memcpy(ssid, target_ssid, ssid_length);

    wifi_scan_config_t scan_config = {
        .ssid = ssid,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 100,
            .max = 300,
        },
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Diagnose-Scan fuer SSID '%s' fehlgeschlagen: %s", target_ssid,
                 esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Anzahl der Diagnose-Scan-Ergebnisse konnte nicht gelesen werden: %s",
                 esp_err_to_name(err));
        return;
    }
    if (ap_count == 0) {
        ESP_LOGW(TAG, "Diagnose-Scan: SSID '%s' wurde nicht gefunden", target_ssid);
        return;
    }

    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(*ap_records));
    if (ap_records == NULL) {
        ESP_LOGW(TAG, "Diagnose-Scan: nicht genug Speicher fuer %u Ergebnisse", ap_count);
        esp_wifi_clear_ap_list();
        return;
    }

    uint16_t record_count = ap_count;
    err = esp_wifi_scan_get_ap_records(&record_count, ap_records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Diagnose-Scan-Ergebnisse konnten nicht gelesen werden: %s",
                 esp_err_to_name(err));
        free(ap_records);
        return;
    }

    for (uint16_t i = 0; i < record_count; ++i) {
        ESP_LOGW(TAG, "Diagnose-Scan: SSID '%s', BSSID %02x:%02x:%02x:%02x:%02x:%02x, "
                 "Kanal %u, RSSI %d dBm, Sicherheit %s (%d), Pairwise-Cipher %s, "
                 "Group-Cipher %s", target_ssid,
                 ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                 ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5],
                 ap_records[i].primary, ap_records[i].rssi,
                 wifi_authmode_name(ap_records[i].authmode), ap_records[i].authmode,
                 wifi_cipher_name(ap_records[i].pairwise_cipher),
                 wifi_cipher_name(ap_records[i].group_cipher));
    }
    free(ap_records);
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
        ESP_LOGI(TAG, "WLAN-Station gestartet; erster Verbindungsversuch");
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Erster WLAN-Verbindungsversuch konnte nicht gestartet werden: %s",
                     esp_err_to_name(err));
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnection = event_data;
        if (disconnection != NULL) {
            ESP_LOGW(TAG, "WLAN getrennt: Grund %u (%s), SSID '%.*s', "
                     "BSSID %02x:%02x:%02x:%02x:%02x:%02x, RSSI %d dBm",
                     disconnection->reason, wifi_disconnect_reason_name(disconnection->reason),
                     (int) disconnection->ssid_len, disconnection->ssid,
                     disconnection->bssid[0], disconnection->bssid[1],
                     disconnection->bssid[2], disconnection->bssid[3],
                     disconnection->bssid[4], disconnection->bssid[5],
                     disconnection->rssi);
        } else {
            ESP_LOGW(TAG, "WLAN getrennt; der Treiber lieferte keinen Abbruchgrund");
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_wifi_had_ip) {
            ESP_LOGW(TAG, "WLAN nach erfolgreicher Verbindung verloren; verbinde erneut");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "WLAN-Wiederverbindung konnte nicht gestartet werden: %s",
                         esp_err_to_name(err));
            }
        } else if (s_wifi_retry_count < WIFI_MAXIMUM_RETRIES) {
            ESP_LOGI(TAG, "WLAN-Verbindungsversuch %d von %d",
                     s_wifi_retry_count + 2, WIFI_MAXIMUM_RETRIES + 1);
            esp_err_t err = esp_wifi_connect();
            if (err == ESP_OK) {
                s_wifi_retry_count++;
            } else {
                ESP_LOGW(TAG, "WLAN-Verbindungsversuch konnte nicht gestartet werden: %s",
                         esp_err_to_name(err));
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
            }
        } else {
            ESP_LOGW(TAG, "WLAN-Verbindung nach %d Versuchen noch nicht hergestellt",
                     WIFI_MAXIMUM_RETRIES + 1);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *connection = event_data;
        if (connection != NULL) {
            ESP_LOGI(TAG, "Mit Access Point verbunden: SSID '%.*s', "
                     "BSSID %02x:%02x:%02x:%02x:%02x:%02x, Kanal %u, Sicherheitsmodus %d",
                     (int) connection->ssid_len, connection->ssid,
                     connection->bssid[0], connection->bssid[1], connection->bssid[2],
                     connection->bssid[3], connection->bssid[4], connection->bssid[5],
                     connection->channel, connection->authmode);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        if (got_ip != NULL) {
            ESP_LOGI(TAG, "DHCP erfolgreich: IP " IPSTR ", Gateway " IPSTR ", Netzmaske " IPSTR,
                     IP2STR(&got_ip->ip_info.ip), IP2STR(&got_ip->ip_info.gw),
                     IP2STR(&got_ip->ip_info.netmask));
        }
        esp_err_t ipv6_err = esp_netif_create_ip6_linklocal(s_wifi_sta_netif);
        if (ipv6_err != ESP_OK) {
            ESP_LOGW(TAG, "IPv6-Link-Local-Adresse konnte nicht erstellt werden: %s",
                     esp_err_to_name(ipv6_err));
        }
        s_wifi_retry_count = 0;
        s_wifi_had_ip = true;
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
        ESP_LOGI(TAG, "Verbinde mit SSID '%s' (WPA2-Enterprise, Benutzername hinterlegt)",
                 config->wifi_ssid);
    } else {
        memcpy(wifi_config.sta.password, config->wifi_password,
               strlen(config->wifi_password));
        /* Permit open networks as well as password-protected ones. The Wi-Fi
         * stack still verifies the supplied password when the selected AP needs
         * one. */
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "Verbinde mit SSID '%s' (%s)", config->wifi_ssid,
                 config->wifi_password[0] == '\0' ? "ohne Passwort" : "Passwort hinterlegt");
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    if (use_enterprise && !configure_enterprise_wifi(config)) {
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WLAN-Energiesparmodus deaktiviert fuer kurze HTTP-Reaktionszeit");

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
        ESP_LOGI(TAG, "WLAN noch nicht verbunden; erneuter Versuch in %u ms",
                 WIFI_RETRY_PAUSE_MS);
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
        ESP_LOGW(TAG, "Keine WLAN-IP fuer SSID '%s' nach %lu ms",
                 config->wifi_ssid,
                 (unsigned long) (timeout_ms == 0 ? WIFI_CONNECTION_TIMEOUT_MS : timeout_ms));
        wifi_ap_record_t connected_ap;
        if (esp_wifi_sta_get_ap_info(&connected_ap) == ESP_OK) {
            ESP_LOGW(TAG, "Access Point ist verbunden, aber DHCP hat noch keine IP geliefert");
        } else {
            log_target_wifi_security(config->wifi_ssid);
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
}
