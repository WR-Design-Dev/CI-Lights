#include "app_config.h"
#include "app_settings.h"
#include "app_web_assets.h"
#include "jenkins_client.h"
#include "traffic_light.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "bootloader_random.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"

#define PROVISION_REQUEST_MAX_LENGTH 1024
#define STATUS_REQUEST_MAX_LENGTH 512
#define JENKINS_CONFIG_REQUEST_MAX_LENGTH 512
#define JENKINS_JOB_LIST_MAX_LENGTH 16384
#define JENKINS_JOB_LIST_URL_MAX_LENGTH 512
#define JENKINS_JOB_PAGE_SIZE 20
#define STATUS_HTTP_TASK_STACK_SIZE 20480
#define CAPTIVE_DNS_PORT 53
#define CAPTIVE_DNS_PACKET_MAX_LENGTH 512
#define DHCPS_OFFER_DNS 0x02

#define LANGUAGE_SWITCH_HTML \
    "<div class=\"language-switch\" role=\"group\" aria-label=\"Sprache\">" \
    "<button type=\"button\" data-language=\"de\" aria-label=\"Deutsch\" title=\"Deutsch\">" \
    "<svg class=\"flag\" viewBox=\"0 0 60 36\" aria-hidden=\"true\">" \
    "<rect width=\"60\" height=\"12\" fill=\"#000\"/>" \
    "<rect y=\"12\" width=\"60\" height=\"12\" fill=\"#dd0000\"/>" \
    "<rect y=\"24\" width=\"60\" height=\"12\" fill=\"#ffce00\"/>" \
    "</svg></button>" \
    "<button type=\"button\" data-language=\"en\" aria-label=\"Englisch\" title=\"Englisch\">" \
    "<svg class=\"flag\" viewBox=\"0 0 60 36\" aria-hidden=\"true\">" \
    "<rect width=\"60\" height=\"36\" fill=\"#012169\"/>" \
    "<path d=\"M0 0L60 36M60 0L0 36\" stroke=\"#fff\" stroke-width=\"12\"/>" \
    "<path d=\"M0 0L60 36M60 0L0 36\" stroke=\"#c8102e\" stroke-width=\"4\"/>" \
    "<path d=\"M30 0V36M0 18H60\" stroke=\"#fff\" stroke-width=\"12\"/>" \
    "<path d=\"M30 0V36M0 18H60\" stroke=\"#c8102e\" stroke-width=\"6\"/>" \
    "</svg></button></div>"

static const char *TAG = "provision";
static const char *HTTP_TAG = "lights_http";

static bool request_has_admin_credentials(httpd_req_t *request)
{
    char authorization[192];
    if (httpd_req_get_hdr_value_str(request, "Authorization", authorization,
                                    sizeof(authorization)) != ESP_OK ||
        strncmp(authorization, "Basic ", 6) != 0) {
        return false;
    }

    unsigned char credentials[APP_ADMIN_USERNAME_MAX_LENGTH + 1 +
                              APP_ADMIN_PASSWORD_MAX_LENGTH + 1];
    size_t length = 0;
    bool authorized = false;
    if (mbedtls_base64_decode(credentials, sizeof(credentials) - 1, &length,
                              (const unsigned char *) authorization + 6,
                              strlen(authorization + 6)) == 0 &&
        length < sizeof(credentials) &&
        memchr(credentials, '\0', length) == NULL) {
        credentials[length] = '\0';
        unsigned char *separator = memchr(credentials, ':', length);
        if (separator != NULL) {
            *separator = '\0';
            authorized = app_admin_credentials_verify((const char *) credentials,
                                                       (const char *) separator + 1);
        }
    }
    mbedtls_platform_zeroize(credentials, sizeof(credentials));
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    return authorized;
}

static esp_err_t protected_handler(httpd_req_t *request)
{
    if (app_admin_password_is_set() && !request_has_admin_credentials(request)) {
        httpd_resp_set_status(request, "401 Unauthorized");
        char ui_request[2];
        if (httpd_req_get_hdr_value_str(request, "X-Admin-UI", ui_request,
                                        sizeof(ui_request)) != ESP_OK ||
            strcmp(ui_request, "1") != 0) {
            httpd_resp_set_hdr(request, "WWW-Authenticate",
                               "Basic realm=\"CI-Lights\", charset=\"UTF-8\"");
        }
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_sendstr(request, "Verwaltungspasswort erforderlich.");
    }
    const httpd_uri_t *original = request->user_ctx;
    return original->handler(request);
}

static esp_err_t register_protected_uri(httpd_handle_t server, const httpd_uri_t *uri)
{
    httpd_uri_t protected_uri = *uri;
    protected_uri.handler = protected_handler;
    protected_uri.user_ctx = (void *) uri;
    return httpd_register_uri_handler(server, &protected_uri);
}

static esp_err_t language_get_handler(httpd_req_t *request)
{
    char response[20];
    snprintf(response, sizeof(response), "{\"language\":\"%s\"}", app_config_get_language());
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t language_post_handler(httpd_req_t *request)
{
    char body[64];
    if (request->content_len <= 0 || request->content_len >= sizeof(body)) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Ungueltige Sprache.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "Sprache konnte nicht gelesen werden.");
        }
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *language = cJSON_GetObjectItemCaseSensitive(json, "language");
    bool valid = cJSON_IsString(language) && language->valuestring != NULL &&
                 (strcmp(language->valuestring, "de") == 0 ||
                  strcmp(language->valuestring, "en") == 0);
    esp_err_t err = valid ? app_config_save_language(language->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, valid ? "500 Internal Server Error" : "400 Bad Request");
        return httpd_resp_sendstr(request, valid ?
                                  "Sprache konnte nicht gespeichert werden." :
                                  "Ungueltige Sprache.");
    }
    return language_get_handler(request);
}

static esp_err_t site_title_get_handler(httpd_req_t *request)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || cJSON_AddStringToObject(json, "title", app_config_get_site_title()) == NULL) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Titel konnte nicht geladen werden.");
    }
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (response == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Titel konnte nicht geladen werden.");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, response);
    free(response);
    return err;
}

static esp_err_t site_title_post_handler(httpd_req_t *request)
{
    char body[512];
    if (request->content_len <= 0 || request->content_len >= sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Ungueltiger Titel.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Titel konnte nicht gelesen werden.");
        }
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
    bool valid = cJSON_IsString(title) && title->valuestring != NULL;
    esp_err_t err = valid ? app_config_save_site_title(title->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, valid && err != ESP_ERR_INVALID_ARG ?
                                   HTTPD_500_INTERNAL_SERVER_ERROR : HTTPD_400_BAD_REQUEST,
                                   valid && err != ESP_ERR_INVALID_ARG ?
                                   "Titel konnte nicht gespeichert werden." : "Ungueltiger Titel.");
    }
    return site_title_get_handler(request);
}

static httpd_handle_t s_provision_server;
static httpd_handle_t s_status_server;
static bool s_restart_pending;
static bool s_wifi_scan_running;
static char s_provision_ssid[33];
static char s_provision_url[24];
static esp_ip4_addr_t s_provision_ip;
static char s_light_hostname[19];
static app_config_t s_status_config;
static app_job_selected_handler_t s_job_selected_handler;
static app_manual_light_handler_t s_manual_light_handler;
static app_manual_light_state_handler_t s_manual_light_state_handler;
static app_control_mode_handler_t s_control_mode_handler;
static app_control_mode_state_handler_t s_control_mode_state_handler;
static app_api_status_handler_t s_api_status_handler;
static app_disco_effect_handler_t s_disco_effect_handler;
static app_disco_effect_state_handler_t s_disco_effect_state_handler;
static app_light_brightness_handler_t s_light_brightness_handler;
static app_light_brightness_state_handler_t s_light_brightness_state_handler;

static void status_http_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    (void) arg;
    (void) event_base;

    if (event_id == HTTP_SERVER_EVENT_ERROR && event_data != NULL) {
        httpd_err_code_t code = *(const httpd_err_code_t *) event_data;
        ESP_LOGW(HTTP_TAG, "HTTP-Serverfehler %d; interner Heap frei %lu B",
                 code, (unsigned long) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    } else if (event_id == HTTP_SERVER_EVENT_ON_CONNECTED && event_data != NULL) {
        ESP_LOGI(HTTP_TAG, "HTTP-Verbindung angenommen: Socket %d", *(const int *) event_data);
    } else if (event_id == HTTP_SERVER_EVENT_DISCONNECTED && event_data != NULL) {
        ESP_LOGI(HTTP_TAG, "HTTP-Verbindung geschlossen: Socket %d", *(const int *) event_data);
    }
}

static esp_err_t send_provision_error(httpd_req_t *request, const char *status,
                                      const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t provision_page_handler(httpd_req_t *request)
{
    app_config_t existing_config;
    bool password_only_setup = !app_admin_password_is_set() &&
                               app_config_load(&existing_config);
    mbedtls_platform_zeroize(&existing_config, sizeof(existing_config));
    uint8_t mac[6];
    char hostname[sizeof(s_light_hostname)] = "unbekannt";
    char mac_address[18] = "unbekannt";
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        snprintf(hostname, sizeof(hostname), "ci-lights-%02x%02x", mac[4], mac[5]);
        snprintf(mac_address, sizeof(mac_address), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "MAC-Adresse konnte nicht gelesen werden: %s", esp_err_to_name(err));
    }

    static const char page_template[] =
        "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Gerät einrichten</title>"
        "<link rel=\"icon\" href=\"/favicon.ico\" sizes=\"any\">"
        "<style>:root{--navy:#1f2937;--blue:#2563eb;--green:#16a34a;--ink:#102a43;--muted:#5c7080;--surface:#fff}"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:linear-gradient(145deg,#eef5fb,#f7fbf3);color:var(--ink);font-family:system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
        ".page{position:relative;max-width:42rem;margin:auto;padding:clamp(1rem,4vw,2rem)}.brand{display:flex;justify-content:center;align-items:center;max-height:16.666vh;margin:0 0 1rem}.language-switch{display:flex;align-items:center;gap:.2rem;width:max-content;margin:0 0 .75rem auto;padding:.2rem;border:1px solid #dbe7ef;border-radius:.7rem;background:#fff}.language-switch button{width:auto;min-height:0;margin:0;padding:.25rem .35rem;border-radius:.4rem;background:transparent;box-shadow:none;line-height:1}.language-switch .flag{display:block;width:1.8rem;height:1.1rem;border-radius:.12rem;box-shadow:0 0 0 1px rgba(16,42,67,.18)}.language-switch button[aria-pressed=\"true\"]{background:#dcebf9;outline:2px solid var(--navy)}"
        ".brand img{display:block;max-width:100%%;max-height:16.666vh;width:auto;height:auto;object-fit:contain}.card{background:var(--surface);border:1px solid #dbe7ef;border-radius:1.25rem;padding:clamp(1.25rem,5vw,2rem);box-shadow:0 1rem 2.5rem rgba(31,41,55,.12)}"
        ".portal-layout{display:grid;grid-template-columns:minmax(0,1fr) 4.7rem;gap:1rem;align-items:stretch}.portal-layout>div{grid-column:1;grid-row:1}.traffic-light{display:flex;grid-column:2;grid-row:1;min-height:100%%;flex-direction:column;align-items:center;filter:drop-shadow(0 .35rem .55rem rgba(31,41,55,.18))}.traffic-light-top{display:block;width:100%%;height:auto;flex:0 0 auto;margin-bottom:-.55rem;position:relative;z-index:1}.traffic-light-middle{width:1.05rem;min-height:1rem;flex:1 1 auto;background:linear-gradient(90deg,#1f2937 0,#1f2937 34%%,#2563eb 34%%,#2563eb 55%%,#1f2937 55%%)}.traffic-light-base{width:100%%;height:1rem;flex:0 0 auto;background:linear-gradient(145deg,#374151,#111827);clip-path:polygon(10%% 0,90%% 0,100%% 100%%,0 100%%)}.intro{min-width:0}.device-name{margin:0 0 .25rem;color:var(--navy);font-size:1.1rem;font-weight:800}"
        ".eyebrow{margin:0;color:var(--blue);font-size:.78rem;font-weight:800;letter-spacing:.09em;text-transform:uppercase}h1{margin:.3rem 0 .5rem;color:var(--navy);font-size:clamp(1.7rem,7vw,2.35rem)}"
        "p{line-height:1.55}label{display:block;margin-top:1rem;font-weight:700}input{width:100%%;margin-top:.4rem;border:1px solid #b8c9d6;border-radius:.7rem;padding:.85rem;font:inherit}input:focus{outline:3px solid #b9d7fa;border-color:var(--blue)}.wifi-name-field{position:relative}.wifi-name-field input{padding-right:3.1rem}.wifi-name-field button{position:absolute;right:.3rem;bottom:.3rem;width:2.45rem;min-height:0;margin:0;padding:.45rem;border-radius:.5rem;font-size:1.35rem;line-height:1;color:#fff}"
        "button{width:100%%;margin-top:1.4rem;border:0;border-radius:.7rem;background:var(--navy);color:#fff;padding:.9rem 1rem;font:700 1rem inherit;box-shadow:0 .35rem .8rem rgba(31,41,55,.2)}button:active{transform:translateY(1px)}"
        ".notice{padding:.85rem 1rem;border-left:.3rem solid var(--green);background:#f1f9e7;border-radius:.4rem}.meta{display:grid;gap:.5rem;padding:1rem;background:#f4f8fb;border:1px solid #d3e2ee;border-radius:.7rem}.meta-label{color:var(--muted);font-size:.88rem;font-weight:700}.device-url,.device-mac{display:block;word-break:break-word;color:var(--navy);font-weight:800;line-height:1.25}.device-url{font-size:clamp(1.2rem,5vw,1.7rem)}.device-mac{font-size:clamp(1.1rem,4.5vw,1.45rem);letter-spacing:.04em}#status{min-height:1.5rem;white-space:pre-wrap;color:var(--navy);font-weight:600}"
        "@media(max-width:360px){.page{padding:.75rem}.card{padding:1.1rem;border-radius:1rem}.portal-layout{grid-template-columns:minmax(0,1fr) 3.5rem;gap:.7rem}}</style></head><body>"
        "<main class=\"page\">" LANGUAGE_SWITCH_HTML
        "<header class=\"brand\" style=\"display:none\"><img src=\"/logo.svg\" alt=\"Logo\" onload=\"this.parentElement.style.display=\'flex\'\" onerror=\"this.parentElement.style.display=\'none\'\"></header><section class=\"card\">"
        "<div class=\"portal-layout\"><aside class=\"traffic-light\" aria-label=\"CI-Lights\"><img class=\"traffic-light-top\" src=\"/traffic-light.svg\" alt=\"CI-Lights\"><div class=\"traffic-light-middle\"></div><div class=\"traffic-light-base\"></div></aside><div>"
        "<div class=\"intro\"><p class=\"device-name\">CI-Lights</p><h1>%s</h1></div>"
        "<div class=\"meta\"><span class=\"meta-label\"><strong>Bitte notieren:</strong> Verwaltungsseite nach dem Neustart</span><code class=\"device-url\">http://%s.local</code><span class=\"meta-label\">WLAN-MAC</span><code class=\"device-mac\">%s</code></div>"
        "<p class=\"notice\">Zurücksetzen: Bei laufendem ESP BOOT fünf Sekunden gedrückt halten.</p>"
        "<form id=\"config\">%s"
        "%s"
        "%s"
        "%s"
        "%s"
        "<button type=\"submit\">Speichern und neu starten</button></form><p id=\"status\"></p></div></div></section></main>"
        "<script src=\"/localization.js\"></script><script>const form=document.querySelector('#config'),networks=document.querySelector('#wifi-networks'),status=document.querySelector('#status'),refreshNetworks=document.querySelector('#refresh-networks');let provisionStatusSource='';function showProvisionStatus(message){provisionStatusSource=message;status.textContent=message.startsWith('Fehler: ')?uiI18n.t('Fehler: ')+uiI18n.response(message.slice(8)):uiI18n.response(message)}"
        "async function loadNetworks(){refreshNetworks.disabled=true;networks.replaceChildren();showProvisionStatus('Suche nach WLANs ...');const r=await fetch('/api/wifi-networks');if(!r.ok)throw new Error(await r.text());const data=await r.json();for(const name of data.networks)networks.append(new Option(name,name));refreshNetworks.disabled=false;showProvisionStatus(data.networks.length?'':'Keine WLANs gefunden. Du kannst den Namen auch manuell eingeben.')}"
        "if(refreshNetworks)refreshNetworks.addEventListener('click',()=>loadNetworks().catch(error=>{refreshNetworks.disabled=false;showProvisionStatus('Fehler: '+error.message)}));"
        "form.addEventListener('submit',async e=>{e.preventDefault();"
        "const password=form.elements.admin_password,confirmation=form.elements.admin_password_confirm;"
        "if(password&&password.value!==confirmation.value){showProvisionStatus('Fehler: Die Verwaltungspasswörter stimmen nicht überein.');return}"
        "const r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(Object.fromEntries(new FormData(e.target)))});"
        "showProvisionStatus(await r.text());});uiI18n.onChange(()=>showProvisionStatus(provisionStatusSource));uiI18n.start();if(refreshNetworks)loadNetworks().catch(error=>{refreshNetworks.disabled=false;showProvisionStatus('Fehler: '+error.message)});</script></body></html>";

    const char *wifi_name_field = password_only_setup ? "" :
        "<label>WLAN-Name<div class=\"wifi-name-field\"><input id=\"wifi-ssid\" name=\"wifi_ssid\" list=\"wifi-networks\" autocomplete=\"username\" placeholder=\"WLAN auswählen oder eingeben\" required maxlength=\"32\"><button id=\"refresh-networks\" type=\"button\" aria-label=\"WLANs aktualisieren\" title=\"WLANs aktualisieren\">&#x21bb;</button></div><datalist id=\"wifi-networks\"></datalist></label>";
    const char *wifi_user_field = password_only_setup ? "" :
        "<label>WLAN-Benutzername (nur WPA2-Enterprise)<input name=\"wifi_username\" type=\"text\" autocomplete=\"username\" maxlength=\"127\"></label>";
    const char *wifi_password_field = password_only_setup ? "" :
        "<label>WLAN-Passwort (bei offenen WLANs leer lassen)<input name=\"wifi_password\" type=\"password\" autocomplete=\"current-password\" maxlength=\"63\"></label>";
    const char *wifi_notice = password_only_setup ?
        "<p class=\"notice\">Gespeicherte WLANs bleiben erhalten.</p>" :
        "<p class=\"notice\">WPA2-Enterprise: Benutzername und Passwort eintragen. Es wird PEAP oder TTLS mit MSCHAPv2 versucht. Achtung: Die Server-Zertifikatsprüfung ist für diesen Test deaktiviert.</p>";
    const char *password_fields = app_admin_password_is_set() ?
        "<p class=\"notice\">Der Verwaltungszugang ist bereits eingerichtet und bleibt erhalten.</p>" :
        "<p class=\"notice\">Lege einen Benutzernamen und ein Verwaltungspasswort fest.</p>"
        "<label>Verwaltungsbenutzername<input name=\"admin_username\" type=\"text\" autocomplete=\"username\" required minlength=\"3\" maxlength=\"32\" pattern=\"[A-Za-z0-9][A-Za-z0-9._-]{2,31}\"></label>"
        "<p class=\"hint\">3 bis 32 Zeichen: Buchstaben, Ziffern, Punkt, Unterstrich und Bindestrich.</p>"
        "<label>Verwaltungspasswort<input name=\"admin_password\" type=\"password\" autocomplete=\"new-password\" required minlength=\"8\" maxlength=\"64\"></label>"
        "<label>Verwaltungspasswort wiederholen<input name=\"admin_password_confirm\" type=\"password\" autocomplete=\"new-password\" required minlength=\"8\" maxlength=\"64\"></label>";
    int page_length = snprintf(NULL, 0, page_template,
                               password_only_setup ? "Verwaltungszugang einrichten" : "WLAN einrichten",
                               hostname, mac_address, wifi_name_field, wifi_user_field,
                               wifi_password_field, wifi_notice, password_fields);
    if (page_length < 0) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "Einrichtungsseite konnte nicht erstellt werden.");
    }
    char *page = malloc((size_t) page_length + 1);
    if (page == NULL) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "Nicht genug Speicher fuer die Einrichtungsseite.");
    }
    snprintf(page, (size_t) page_length + 1, page_template,
             password_only_setup ? "Verwaltungszugang einrichten" : "WLAN einrichten",
             hostname, mac_address, wifi_name_field, wifi_user_field,
             wifi_password_field, wifi_notice, password_fields);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    esp_err_t send_err = httpd_resp_send(request, page, page_length);
    free(page);
    return send_err;
}

/* iOS requests this URL after joining a Wi-Fi network. A redirect to the
 * setup page makes its captive-portal window render the local form. */
static esp_err_t provision_redirect_handler(httpd_req_t *request)
{
    ESP_LOGI(TAG, "Captive-Portal-Anfrage '%s' wird zur Einrichtungsseite umgeleitet",
             request->uri);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, "Weiterleitung zur CI-Lights-Einrichtung.");
}

/* Other connectivity-check URLs use the same redirect. */
static esp_err_t provision_not_found_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void) error;
    return provision_redirect_handler(request);
}

/* Accept a standard one-question A record lookup and point it at the SoftAP.
 * Captive-portal clients use DNS before issuing their HTTP probe. */
static size_t create_captive_dns_response(const uint8_t *request, size_t request_length,
                                          uint8_t *response, size_t response_capacity)
{
    const size_t dns_header_length = 12;
    const size_t dns_question_length = 4;
    const size_t dns_answer_length = 16;
    if (request_length < dns_header_length || request[4] != 0 || request[5] != 1) {
        return 0;
    }

    size_t question_end = dns_header_length;
    while (question_end < request_length) {
        uint8_t label_length = request[question_end++];
        if (label_length == 0) {
            break;
        }
        if (label_length > 63 || question_end + label_length > request_length) {
            return 0;
        }
        question_end += label_length;
    }
    if (question_end + dns_question_length > request_length ||
        question_end + dns_question_length + dns_answer_length > response_capacity) {
        return 0;
    }

    uint16_t question_type = ((uint16_t) request[question_end] << 8) |
                             request[question_end + 1];
    uint16_t question_class = ((uint16_t) request[question_end + 2] << 8) |
                              request[question_end + 3];
    if (question_type != 1 || question_class != 1) {
        return 0;
    }

    size_t response_length = question_end + dns_question_length;
    memcpy(response, request, response_length);
    response[2] = 0x80 | (request[2] & 0x79);  // response, preserve RD and opcode
    response[3] = 0;
    response[6] = 0;
    response[7] = 1;
    memset(response + 8, 0, 4);

    uint8_t *answer = response + response_length;
    const uint8_t prefix[] = {
        0xc0, 0x0c,             // pointer to the requested name
        0x00, 0x01,             // A record
        0x00, 0x01,             // Internet class
        0x00, 0x00, 0x00, 0x3c, // 60 second TTL
        0x00, 0x04              // IPv4 address length
    };
    memcpy(answer, prefix, sizeof(prefix));
    memcpy(answer + sizeof(prefix), &s_provision_ip.addr, sizeof(s_provision_ip.addr));
    return response_length + dns_answer_length;
}

static void captive_dns_task(void *argument)
{
    (void) argument;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "Captive-Portal-DNS konnte nicht gestartet werden: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CAPTIVE_DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "Captive-Portal-DNS konnte Port 53 nicht binden: errno %d", errno);
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        uint8_t request[CAPTIVE_DNS_PACKET_MAX_LENGTH];
        uint8_t response[CAPTIVE_DNS_PACKET_MAX_LENGTH];
        struct sockaddr_storage client_address;
        socklen_t client_address_length = sizeof(client_address);
        int request_length = recvfrom(socket_fd, request, sizeof(request), 0,
                                      (struct sockaddr *) &client_address,
                                      &client_address_length);
        if (request_length < 0) {
            ESP_LOGE(TAG, "Captive-Portal-DNS empfaengt keine Daten mehr: errno %d", errno);
            break;
        }

        size_t response_length = create_captive_dns_response(request, (size_t) request_length,
                                                              response, sizeof(response));
        if (response_length > 0 &&
            sendto(socket_fd, response, response_length, 0,
                   (struct sockaddr *) &client_address, client_address_length) < 0) {
            ESP_LOGW(TAG, "Captive-Portal-DNS konnte nicht antworten: errno %d", errno);
        }
    }

    close(socket_fd);
    vTaskDelete(NULL);
}

static void configure_captive_portal_dhcp(esp_netif_t *ap_netif)
{
    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));
    s_provision_ip = ip_info.ip;
    snprintf(s_provision_url, sizeof(s_provision_url), "http://" IPSTR "/",
             IP2STR(&s_provision_ip));

    esp_netif_dns_info_t dns_info = {0};
    dns_info.ip.type = IPADDR_TYPE_V4;
    dns_info.ip.u_addr.ip4 = s_provision_ip;
    uint8_t offer_dns = DHCPS_OFFER_DNS;

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                                           ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &offer_dns, sizeof(offer_dns)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                                           ESP_NETIF_CAPTIVEPORTAL_URI,
                                           s_provision_url, strlen(s_provision_url)));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
}

static bool wifi_config_from_json(cJSON *json, app_config_t *config)
{
    cJSON *wifi_ssid = cJSON_GetObjectItemCaseSensitive(json, "wifi_ssid");
    cJSON *wifi_username = cJSON_GetObjectItemCaseSensitive(json, "wifi_username");
    cJSON *wifi_password = cJSON_GetObjectItemCaseSensitive(json, "wifi_password");

    if (!cJSON_IsString(wifi_ssid) || !cJSON_IsString(wifi_password) ||
        (wifi_username != NULL && !cJSON_IsString(wifi_username)) ||
        !app_config_copy_string(config->wifi_ssid, sizeof(config->wifi_ssid),
                     wifi_ssid->valuestring, true) ||
        !app_config_copy_string(config->wifi_password, sizeof(config->wifi_password),
                     wifi_password->valuestring, false)) {
        return false;
    }

    config->wifi_username[0] = '\0';
    if (wifi_username != NULL &&
        !app_config_copy_string(config->wifi_username, sizeof(config->wifi_username),
                                wifi_username->valuestring, false)) {
        return false;
    }

    return app_config_wifi_is_valid(config);
}

static bool jenkins_config_from_json(cJSON *json, app_config_t *config)
{
    cJSON *jenkins_url = cJSON_GetObjectItemCaseSensitive(json, "jenkins_url");
    cJSON *jenkins_user = cJSON_GetObjectItemCaseSensitive(json, "jenkins_user");
    cJSON *jenkins_token = cJSON_GetObjectItemCaseSensitive(json, "jenkins_token");

    if (!cJSON_IsString(jenkins_url) || !cJSON_IsString(jenkins_user) ||
        !cJSON_IsString(jenkins_token) ||
        !app_config_copy_string(config->jenkins_url, sizeof(config->jenkins_url),
                     jenkins_url->valuestring, true) ||
        !app_config_copy_string(config->jenkins_user, sizeof(config->jenkins_user),
                     jenkins_user->valuestring, true) ||
        !app_config_copy_string(config->jenkins_token, sizeof(config->jenkins_token),
                     jenkins_token->valuestring, true)) {
        return false;
    }

    size_t url_length = strlen(config->jenkins_url);
    while (url_length > 8 && config->jenkins_url[url_length - 1] == '/') {
        config->jenkins_url[--url_length] = '\0';
    }

    config->jenkins_job_path[0] = '\0';
    return app_config_jenkins_is_valid(config);
}

static void restart_after_provisioning(void *argument)
{
    (void) argument;
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t provision_config_handler(httpd_req_t *request)
{
    if (s_restart_pending) {
        return send_provision_error(request, "409 Conflict", "Neustart wird bereits vorbereitet.");
    }
    if (request->content_len == 0 || request->content_len >= PROVISION_REQUEST_MAX_LENGTH) {
        return send_provision_error(request, "400 Bad Request", "Ungültige Konfigurationsdaten.");
    }

    char request_body[PROVISION_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_provision_error(request, "400 Bad Request", "Daten konnten nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    app_config_t config = {0};
    bool wifi_supplied = json != NULL &&
                         cJSON_GetObjectItemCaseSensitive(json, "wifi_ssid") != NULL;
    bool valid = json != NULL &&
                 (wifi_supplied ? wifi_config_from_json(json, &config) :
                  !app_admin_password_is_set() && app_config_load(&config));
    if (!valid) {
        cJSON_Delete(json);
        return send_provision_error(request, "400 Bad Request",
                                    "Bitte WLAN-Name und WLAN-Passwort korrekt ausfüllen.");
    }

    if (!app_admin_password_is_set()) {
        const cJSON *username = cJSON_GetObjectItemCaseSensitive(json, "admin_username");
        const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "admin_password");
        const cJSON *confirmation = cJSON_GetObjectItemCaseSensitive(json,
                                                                    "admin_password_confirm");
        if (!cJSON_IsString(username) || !cJSON_IsString(password) ||
            !cJSON_IsString(confirmation) || username->valuestring == NULL ||
            password->valuestring == NULL || confirmation->valuestring == NULL ||
            strcmp(password->valuestring, confirmation->valuestring) != 0 ||
            strlen(password->valuestring) < APP_ADMIN_PASSWORD_MIN_LENGTH ||
            strlen(password->valuestring) > APP_ADMIN_PASSWORD_MAX_LENGTH) {
            cJSON_Delete(json);
            return send_provision_error(request, "400 Bad Request",
                                        "Benutzername und Verwaltungspasswort korrekt eingeben und Passwort wiederholen.");
        }
        esp_err_t password_err = app_admin_credentials_save(username->valuestring,
                                                            password->valuestring);
        if (password_err != ESP_OK) {
            cJSON_Delete(json);
            ESP_LOGE(TAG, "Verwaltungszugang konnte nicht gespeichert werden: %s",
                     esp_err_to_name(password_err));
            return send_provision_error(request,
                                        password_err == ESP_ERR_INVALID_ARG ?
                                        "400 Bad Request" : "500 Internal Server Error",
                                        password_err == ESP_ERR_INVALID_ARG ?
                                        "Benutzername oder Verwaltungspasswort ist ungültig." :
                                        "Verwaltungszugang konnte nicht gespeichert werden.");
        }
    }
    cJSON_Delete(json);

    esp_err_t err = wifi_supplied ? app_config_save_wifi(&config) : ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Konfiguration konnte nicht gespeichert werden: %s", esp_err_to_name(err));
        return send_provision_error(request, "500 Internal Server Error",
                                    "Speichern fehlgeschlagen.");
    }

    s_restart_pending = true;
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_sendstr(request, "Gespeichert. Der ESP startet in zwei Sekunden neu.");
    if (xTaskCreate(restart_after_provisioning, "provision_restart", 2048,
                    NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Neustart-Task konnte nicht erstellt werden");
    }
    return ESP_OK;
}

static esp_err_t status_wifi_get_handler(httpd_req_t *request)
{
    app_wifi_profiles_t profiles;
    if (!app_wifi_profiles_load(&profiles)) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "Gespeicherte WLANs konnten nicht gelesen werden.");
    }
    cJSON *result = cJSON_CreateObject();
    cJSON *networks = result == NULL ? NULL : cJSON_AddArrayToObject(result, "networks");
    if (networks == NULL ||
        cJSON_AddStringToObject(result, "connected_ssid", s_status_config.wifi_ssid) == NULL) {
        cJSON_Delete(result);
        return send_provision_error(request, "500 Internal Server Error",
                                    "Gespeicherte WLANs konnten nicht gelesen werden.");
    }
    for (uint8_t i = 0; i < profiles.count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL ||
            cJSON_AddStringToObject(entry, "ssid", profiles.entries[i].ssid) == NULL ||
            cJSON_AddBoolToObject(entry, "last_used", i == profiles.last_index) == NULL ||
            !cJSON_AddItemToArray(networks, entry)) {
            cJSON_Delete(entry);
            cJSON_Delete(result);
            return send_provision_error(request, "500 Internal Server Error",
                                        "Gespeicherte WLANs konnten nicht gelesen werden.");
        }
    }
    char *body = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (body == NULL) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "Gespeicherte WLANs konnten nicht gelesen werden.");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t status_wifi_delete_handler(httpd_req_t *request)
{
    char body[128];
    if (request->content_len <= 0 || request->content_len >= sizeof(body)) {
        return send_provision_error(request, "400 Bad Request", "Ungueltiger WLAN-Name.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            return send_provision_error(request, "400 Bad Request",
                                        "WLAN-Name konnte nicht gelesen werden.");
        }
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    char ssid[APP_WIFI_SSID_MAX_LENGTH + 1];
    bool valid = cJSON_IsString(ssid_item) &&
                 app_config_copy_string(ssid, sizeof(ssid), ssid_item->valuestring, true);
    cJSON_Delete(json);
    if (!valid) {
        return send_provision_error(request, "400 Bad Request", "Ungueltiger WLAN-Name.");
    }
    esp_err_t err = app_wifi_profiles_remove(ssid);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_provision_error(request, "409 Conflict",
                                    "Das letzte WLAN kann nicht entfernt werden.");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return send_provision_error(request, "404 Not Found", "WLAN nicht gefunden.");
    }
    if (err != ESP_OK) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "WLAN konnte nicht entfernt werden.");
    }
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request,
                              "WLAN entfernt. Die Aenderung gilt nach dem naechsten Neustart.");
}

static esp_err_t provision_wifi_networks_handler(httpd_req_t *request)
{
    if (s_wifi_scan_running) {
        return send_provision_error(request, "409 Conflict",
                                    "Eine WLAN-Suche läuft bereits.");
    }

    s_wifi_scan_running = true;
    wifi_scan_config_t scan_config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = false,
        .scan_time.active = {
            .min = 100,
            .max = 300,
        },
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        s_wifi_scan_running = false;
        ESP_LOGW(TAG, "WLAN-Suche konnte nicht gestartet werden: %s", esp_err_to_name(err));
        return send_provision_error(request, "503 Service Unavailable",
                                    "WLAN-Suche konnte nicht gestartet werden.");
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        s_wifi_scan_running = false;
        ESP_LOGW(TAG, "Anzahl gefundener WLANs konnte nicht gelesen werden: %s",
                 esp_err_to_name(err));
        return send_provision_error(request, "500 Internal Server Error",
                                    "Gefundene WLANs konnten nicht gelesen werden.");
    }

    wifi_ap_record_t *ap_records = NULL;
    if (ap_count > 0) {
        ap_records = calloc(ap_count, sizeof(*ap_records));
        if (ap_records == NULL) {
            esp_wifi_clear_ap_list();
            s_wifi_scan_running = false;
            return send_provision_error(request, "500 Internal Server Error",
                                        "Nicht genug Speicher für die WLAN-Liste.");
        }
        uint16_t record_count = ap_count;
        err = esp_wifi_scan_get_ap_records(&record_count, ap_records);
        if (err != ESP_OK) {
            free(ap_records);
            s_wifi_scan_running = false;
            ESP_LOGW(TAG, "WLAN-Liste konnte nicht gelesen werden: %s", esp_err_to_name(err));
            return send_provision_error(request, "500 Internal Server Error",
                                        "Gefundene WLANs konnten nicht gelesen werden.");
        }
        ap_count = record_count;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *networks = result == NULL ? NULL : cJSON_AddArrayToObject(result, "networks");
    if (networks == NULL) {
        cJSON_Delete(result);
        free(ap_records);
        s_wifi_scan_running = false;
        return send_provision_error(request, "500 Internal Server Error",
                                    "WLAN-Liste konnte nicht erstellt werden.");
    }

    for (uint16_t i = 0; i < ap_count; ++i) {
        char ssid[sizeof(ap_records[i].ssid) + 1];
        memcpy(ssid, ap_records[i].ssid, sizeof(ap_records[i].ssid));
        ssid[sizeof(ap_records[i].ssid)] = '\0';
        if (ssid[0] == '\0') {
            continue;
        }

        bool already_listed = false;
        cJSON *network;
        cJSON_ArrayForEach(network, networks) {
            if (cJSON_IsString(network) && network->valuestring != NULL &&
                strcmp(network->valuestring, ssid) == 0) {
                already_listed = true;
                break;
            }
        }
        if (!already_listed) {
            cJSON *network_name = cJSON_CreateString(ssid);
            if (network_name != NULL) {
                cJSON_AddItemToArray(networks, network_name);
            }
        }
    }
    free(ap_records);
    s_wifi_scan_running = false;

    char *response = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (response == NULL) {
        return send_provision_error(request, "500 Internal Server Error",
                                    "WLAN-Liste konnte nicht erstellt werden.");
    }

    httpd_resp_set_type(request, "application/json; charset=utf-8");
    esp_err_t send_err = httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return send_err;
}

static const httpd_uri_t provision_page_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = provision_page_handler,
};

static const httpd_uri_t logo_uri = {
    .uri = "/logo.svg",
    .method = HTTP_GET,
    .handler = app_web_logo_handler,
};

static const httpd_uri_t favicon_ico_uri = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = app_web_favicon_handler,
};

static const httpd_uri_t branding_status_uri = {
    .uri = "/api/branding",
    .method = HTTP_GET,
    .handler = app_web_branding_status_handler,
};

static const httpd_uri_t branding_upload_uri = {
    .uri = "/api/branding",
    .method = HTTP_PUT,
    .handler = app_web_branding_upload_handler,
};

static const httpd_uri_t traffic_light_uri = {
    .uri = "/traffic-light.svg",
    .method = HTTP_GET,
    .handler = app_web_traffic_light_handler,
};

static const httpd_uri_t localization_js_uri = {
    .uri = "/localization.js",
    .method = HTTP_GET,
    .handler = app_web_localization_handler,
};

static const httpd_uri_t language_get_uri = {
    .uri = "/api/language",
    .method = HTTP_GET,
    .handler = language_get_handler,
};

static const httpd_uri_t language_post_uri = {
    .uri = "/api/language",
    .method = HTTP_POST,
    .handler = language_post_handler,
};

static const httpd_uri_t site_title_get_uri = {
    .uri = "/api/site-title",
    .method = HTTP_GET,
    .handler = site_title_get_handler,
};

static const httpd_uri_t site_title_post_uri = {
    .uri = "/api/site-title",
    .method = HTTP_POST,
    .handler = site_title_post_handler,
};

static const httpd_uri_t provision_config_uri = {
    .uri = "/configure",
    .method = HTTP_POST,
    .handler = provision_config_handler,
};

static const httpd_uri_t provision_wifi_networks_uri = {
    .uri = "/api/wifi-networks",
    .method = HTTP_GET,
    .handler = provision_wifi_networks_handler,
};

static const httpd_uri_t provision_ios_probe_uri = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = provision_redirect_handler,
};

typedef struct {
    char *body;
    size_t length;
} jenkins_response_t;

static esp_err_t send_status_error(httpd_req_t *request, const char *status,
                                   const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t jenkins_response_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    jenkins_response_t *response = event->user_data;
    if (response == NULL ||
        response->length + (size_t) event->data_len >= JENKINS_JOB_LIST_MAX_LENGTH) {
        return ESP_ERR_NO_MEM;
    }

    char *expanded_body = heap_caps_realloc_prefer(
        response->body, response->length + (size_t) event->data_len + 1, 2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (expanded_body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    response->body = expanded_body;
    memcpy(response->body + response->length, event->data, event->data_len);
    response->length += (size_t) event->data_len;
    response->body[response->length] = '\0';
    return ESP_OK;
}

static bool jenkins_job_url_is_valid(const char *url)
{
    size_t base_length = strlen(s_status_config.jenkins_url);
    return url != NULL &&
           strncmp(url, s_status_config.jenkins_url, base_length) == 0 &&
           strncmp(url + base_length, "/job/", 5) == 0 &&
           strpbrk(url + base_length, "?#") == NULL;
}

static cJSON *fetch_jenkins_job_level(const char *parent_url, size_t first_job)
{
    char request_url[JENKINS_JOB_LIST_URL_MAX_LENGTH];
    size_t parent_length = strlen(parent_url);
    int url_length = snprintf(request_url, sizeof(request_url),
                              "%s%sapi/json?tree=jobs%%5Bname,url,_class%%5D%%7B%zu,%zu%%7D",
                              parent_url,
                              parent_length > 0 && parent_url[parent_length - 1] == '/'
                                  ? "" : "/",
                              first_job, first_job + JENKINS_JOB_PAGE_SIZE);
    if (url_length < 0 || url_length >= sizeof(request_url)) {
        ESP_LOGE(TAG, "Jenkins-URL fuer die Jobliste ist zu lang");
        return NULL;
    }

    jenkins_response_t response = {0};
    esp_http_client_config_t client_config = {
        .url = request_url,
        .username = s_status_config.jenkins_user,
        .password = s_status_config.jenkins_token,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .event_handler = jenkins_response_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Jenkins-Verbindung konnte nicht vorbereitet werden");
        return NULL;
    }

    int64_t request_start_us = esp_timer_get_time();
    ESP_LOGI(HTTP_TAG, "GET /api/jobs: Jenkins-Ebene wird geladen");
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(HTTP_TAG, "GET /api/jobs: Jenkins-Ebene nach %lld ms geladen (%s, HTTP %d)",
             (long long) ((esp_timer_get_time() - request_start_us) / 1000),
             esp_err_to_name(err), status_code);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status_code < 200 || status_code >= 300 || response.body == NULL) {
        ESP_LOGE(TAG, "Jobliste konnte nicht geladen werden: %s (HTTP %d)",
                 esp_err_to_name(err), status_code);
        free(response.body);
        return NULL;
    }

    cJSON *jenkins_json = cJSON_Parse(response.body);
    free(response.body);
    if (jenkins_json == NULL ||
        !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(jenkins_json, "jobs"))) {
        cJSON_Delete(jenkins_json);
        ESP_LOGE(TAG, "Jenkins lieferte keine gueltige Jobliste");
        return NULL;
    }
    return jenkins_json;
}

static bool jenkins_item_is_folder(const cJSON *job)
{
    const cJSON *item_class = cJSON_GetObjectItemCaseSensitive(job, "_class");
    if (!cJSON_IsString(item_class) || item_class->valuestring == NULL) {
        return false;
    }
    const char *class_name = item_class->valuestring;
    size_t length = strlen(class_name);
    static const char folder_suffix[] = "Folder";
    static const char multibranch_suffix[] = "MultiBranchProject";
    return (length >= sizeof(folder_suffix) - 1 &&
            strcmp(class_name + length - (sizeof(folder_suffix) - 1), folder_suffix) == 0) ||
           (length >= sizeof(multibranch_suffix) - 1 &&
            strcmp(class_name + length - (sizeof(multibranch_suffix) - 1),
                   multibranch_suffix) == 0);
}

static bool append_jenkins_job_level(cJSON *result_jobs, cJSON *folders,
                                     const cJSON *jenkins_jobs, const char *parent_name)
{
    const cJSON *job;
    cJSON_ArrayForEach(job, jenkins_jobs) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(job, "name");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(job, "url");
        if (!cJSON_IsString(name) || !cJSON_IsString(url) ||
            name->valuestring == NULL || url->valuestring == NULL) {
            continue;
        }

        size_t parent_length = strlen(parent_name);
        size_t name_length = strlen(name->valuestring);
        size_t display_length = parent_length + name_length + (parent_length == 0 ? 1 : 4);
        char *display_name = malloc(display_length);
        if (display_name == NULL) {
            return false;
        }
        snprintf(display_name, display_length, "%s%s%s",
                 parent_name, parent_length == 0 ? "" : " / ", name->valuestring);

        bool is_folder = jenkins_item_is_folder(job);
        if (is_folder && !jenkins_job_url_is_valid(url->valuestring)) {
            ESP_LOGE(TAG, "Jenkins lieferte eine ungueltige Ordner-URL");
            free(display_name);
            return false;
        }
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL ||
            cJSON_AddStringToObject(entry, "name", display_name) == NULL ||
            cJSON_AddStringToObject(entry, "url", url->valuestring) == NULL) {
            cJSON_Delete(entry);
            free(display_name);
            return false;
        }
        free(display_name);
        if (!cJSON_AddItemToArray(is_folder ? folders : result_jobs, entry)) {
            cJSON_Delete(entry);
            return false;
        }
    }
    return true;
}

static esp_err_t status_jobs_handler(httpd_req_t *request)
{
    if (!app_config_jenkins_is_valid(&s_status_config)) {
        return send_status_error(request, "409 Conflict",
                                 "Bitte zuerst Jenkins-URL, Benutzer und API-Token speichern.");
    }

    cJSON *result = cJSON_CreateObject();
    if (result == NULL) {
        return send_status_error(request, "500 Internal Server Error",
                                 "Nicht genug Speicher fuer die Jobliste.");
    }
    cJSON *result_jobs = cJSON_AddArrayToObject(result, "jobs");
    if (result_jobs == NULL) {
        cJSON_Delete(result);
        return send_status_error(request, "500 Internal Server Error",
                                 "Nicht genug Speicher fuer die Jobliste.");
    }

    if (s_status_config.jenkins_job_path[0] != '\0') {
        static const char api_suffix[] = "api/json";
        size_t job_path_length = strlen(s_status_config.jenkins_job_path);
        if (job_path_length > sizeof(api_suffix) - 1 &&
            strcmp(s_status_config.jenkins_job_path + job_path_length -
                   (sizeof(api_suffix) - 1), api_suffix) == 0) {
            char selected_job_url[APP_JENKINS_URL_MAX_LENGTH +
                                  APP_JENKINS_JOB_PATH_MAX_LENGTH + 1];
            int selected_url_length = snprintf(selected_job_url, sizeof(selected_job_url),
                                               "%s%.*s", s_status_config.jenkins_url,
                                               (int) (job_path_length - (sizeof(api_suffix) - 1)),
                                               s_status_config.jenkins_job_path);
            if (selected_url_length >= 0 && selected_url_length < sizeof(selected_job_url) &&
                cJSON_AddStringToObject(result, "selected_url", selected_job_url) == NULL) {
                cJSON_Delete(result);
                return send_status_error(request, "500 Internal Server Error",
                                         "Ausgewaehlter Job konnte nicht gelesen werden.");
            }
        }
    }

    cJSON *folders = cJSON_CreateArray();
    if (folders == NULL) {
        cJSON_Delete(result);
        return send_status_error(request, "500 Internal Server Error",
                                 "Nicht genug Speicher fuer die Jobliste.");
    }
    const char *parent_url = s_status_config.jenkins_url;
    const char *parent_name = "";
    int folder_index = 0;
    for (;;) {
        size_t level_job_count = 0;
        for (size_t first_job = 0;; first_job += JENKINS_JOB_PAGE_SIZE) {
            cJSON *jenkins_json = fetch_jenkins_job_level(parent_url, first_job);
            if (jenkins_json == NULL) {
                cJSON_Delete(folders);
                cJSON_Delete(result);
                return send_status_error(request, "502 Bad Gateway",
                                         "Jenkins-Jobliste konnte nicht vollstaendig geladen werden.");
            }
            const cJSON *jenkins_jobs = cJSON_GetObjectItemCaseSensitive(jenkins_json, "jobs");
            int job_count = cJSON_GetArraySize(jenkins_jobs);
            level_job_count += (size_t) job_count;
            bool appended = append_jenkins_job_level(result_jobs, folders,
                                                      jenkins_jobs, parent_name);
            cJSON_Delete(jenkins_json);
            if (!appended) {
                cJSON_Delete(folders);
                cJSON_Delete(result);
                return send_status_error(request, "500 Internal Server Error",
                                         "Jobliste konnte nicht vollstaendig erstellt werden.");
            }
            if (job_count < JENKINS_JOB_PAGE_SIZE) {
                break;
            }
        }
        ESP_LOGI(HTTP_TAG, "GET /api/jobs: %lu Eintraege in einer Jenkins-Ebene",
                 (unsigned long) level_job_count);
        cJSON *next_folder = cJSON_GetArrayItem(folders, folder_index++);
        if (next_folder == NULL) {
            break;
        }
        parent_url = cJSON_GetObjectItemCaseSensitive(next_folder, "url")->valuestring;
        parent_name = cJSON_GetObjectItemCaseSensitive(next_folder, "name")->valuestring;
    }

    char *result_text = cJSON_PrintBuffered(result, 8192, false);
    cJSON_Delete(result);
    cJSON_Delete(folders);
    if (result_text == NULL) {
        return send_status_error(request, "500 Internal Server Error",
                                 "Jobliste konnte nicht erstellt werden.");
    }

    httpd_resp_set_type(request, "application/json; charset=utf-8");
    esp_err_t send_err = httpd_resp_send(request, result_text, HTTPD_RESP_USE_STRLEN);
    free(result_text);
    ESP_LOGI(HTTP_TAG, "GET /api/jobs: Stackreserve der HTTP-Task %lu B",
             (unsigned long) uxTaskGetStackHighWaterMark(NULL));
    return send_err;
}

static esp_err_t status_jenkins_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len >= JENKINS_CONFIG_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültige Jenkins-Zugangsdaten.");
    }

    char request_body[JENKINS_CONFIG_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Jenkins-Zugangsdaten konnten nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    app_config_t updated_config = s_status_config;
    bool valid = json != NULL && jenkins_config_from_json(json, &updated_config);
    if (json != NULL) {
        cJSON_Delete(json);
    }
    if (!valid) {
        return send_status_error(request, "400 Bad Request",
                                 "Bitte eine HTTPS-Jenkins-URL, Benutzer und API-Token angeben.");
    }

    esp_err_t err = app_config_save_jenkins(&updated_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Jenkins-Zugang konnte nicht gespeichert werden: %s", esp_err_to_name(err));
        return send_status_error(request, "500 Internal Server Error",
                                 "Jenkins-Zugang konnte nicht gespeichert werden.");
    }

    s_status_config = updated_config;
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request,
                              "Jenkins-Zugang gespeichert. Wähle jetzt einen Jenkins-Job aus.");
}

static esp_err_t status_poll_interval_get_handler(httpd_req_t *request)
{
    char response[48];
    int response_length = snprintf(response, sizeof(response), "{\"minutes\":%lu}",
                                   (unsigned long) s_status_config.jenkins_poll_interval_minutes);
    if (response_length < 0 || response_length >= sizeof(response)) {
        return send_status_error(request, "500 Internal Server Error",
                                 "Abfrageintervall konnte nicht gelesen werden.");
    }

    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, response, response_length);
}

static esp_err_t status_poll_interval_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültiges Abfrageintervall.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Abfrageintervall konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *minutes = json == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(json, "poll_interval_minutes");
    double minutes_value = cJSON_IsNumber(minutes) ? cJSON_GetNumberValue(minutes) : 0;
    bool valid = cJSON_IsNumber(minutes) && minutes_value >= 1.0 &&
                 minutes_value <= (double) UINT32_MAX &&
                 minutes_value == (double) (uint32_t) minutes_value;
    if (json != NULL) {
        cJSON_Delete(json);
    }
    if (!valid) {
        return send_status_error(request, "400 Bad Request",
                                 "Bitte ein ganzzahliges Intervall ab 1 Minute angeben.");
    }

    uint32_t poll_interval_minutes = (uint32_t) minutes_value;
    esp_err_t err = app_config_save_jenkins_poll_interval(poll_interval_minutes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Abfrageintervall konnte nicht gespeichert werden: %s",
                 esp_err_to_name(err));
        return send_status_error(request, "500 Internal Server Error",
                                 "Abfrageintervall konnte nicht gespeichert werden.");
    }

    s_status_config.jenkins_poll_interval_minutes = poll_interval_minutes;
    char response[96];
    int response_length = snprintf(response, sizeof(response),
                                   "Jenkins wird alle %lu Minute%s abgefragt.",
                                   (unsigned long) poll_interval_minutes,
                                   poll_interval_minutes == 1 ? "" : "n");
    if (response_length < 0 || response_length >= sizeof(response)) {
        return send_status_error(request, "500 Internal Server Error",
                                 "Abfrageintervall wurde gespeichert.");
    }

    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_send(request, response, response_length);
}

static bool select_jenkins_job(const char *job_url)
{
    if (!jenkins_job_url_is_valid(job_url)) {
        return false;
    }

    const char *job_path = job_url + strlen(s_status_config.jenkins_url);
    size_t job_path_length = strlen(job_path);
    const char *api_suffix = job_path[job_path_length - 1] == '/' ? "api/json" : "/api/json";
    char selected_path[sizeof(s_status_config.jenkins_job_path)];
    int length = snprintf(selected_path, sizeof(selected_path), "%s%s",
                          job_path, api_suffix);
    if (length < 0 || length >= sizeof(selected_path)) {
        return false;
    }
    memcpy(s_status_config.jenkins_job_path, selected_path, length + 1);
    return true;
}

static esp_err_t status_job_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültige Jobauswahl.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Jobauswahl konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *job_url = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "url");
    bool selected = cJSON_IsString(job_url) && job_url->valuestring != NULL &&
                    select_jenkins_job(job_url->valuestring);
    if (json != NULL) {
        cJSON_Delete(json);
    }
    if (!selected) {
        return send_status_error(request, "400 Bad Request", "Ungültige Jobauswahl.");
    }

    esp_err_t err = app_config_save_jenkins(&s_status_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ausgewaehlter Job konnte nicht gespeichert werden: %s",
                 esp_err_to_name(err));
        return send_status_error(request, "500 Internal Server Error",
                                 "Jobauswahl konnte nicht gespeichert werden.");
    }

    if (s_job_selected_handler != NULL) {
        s_job_selected_handler(&s_status_config);
    }

    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, "Job gespeichert. Die Ampel wurde aktualisiert.");
}

static esp_err_t status_jenkins_request_state_handler(httpd_req_t *request)
{
    const char *response = jenkins_client_request_in_progress() ?
                           "{\"in_progress\":true}" : "{\"in_progress\":false}";
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

static const char *control_mode_name(app_control_mode_t mode)
{
    switch (mode) {
    case APP_CONTROL_MODE_AUTO:
        return "auto";
    case APP_CONTROL_MODE_MANUAL:
        return "manual";
    case APP_CONTROL_MODE_DISCO:
        return "disco";
    case APP_CONTROL_MODE_API:
        return "api";
    }
    return "auto";
}

static bool control_mode_from_name(const char *name, app_control_mode_t *mode)
{
    if (strcmp(name, "auto") == 0) {
        *mode = APP_CONTROL_MODE_AUTO;
    } else if (strcmp(name, "manual") == 0) {
        *mode = APP_CONTROL_MODE_MANUAL;
    } else if (strcmp(name, "disco") == 0) {
        *mode = APP_CONTROL_MODE_DISCO;
    } else if (strcmp(name, "api") == 0) {
        *mode = APP_CONTROL_MODE_API;
    } else {
        return false;
    }
    return true;
}

static const char *disco_effect_name(app_disco_effect_t effect)
{
    switch (effect) {
    case APP_DISCO_EFFECT_RAINBOW:
        return "rainbow";
    case APP_DISCO_EFFECT_COLORLOOP:
        return "colorloop";
    case APP_DISCO_EFFECT_CHASE:
        return "chase";
    case APP_DISCO_EFFECT_RAINBOW_CHASE:
        return "rainbow-chase";
    case APP_DISCO_EFFECT_BLINK:
        return "blink";
    case APP_DISCO_EFFECT_BREATHE:
        return "breathe";
    case APP_DISCO_EFFECT_TWINKLE:
        return "twinkle";
    case APP_DISCO_EFFECT_SCAN:
        return "scan";
    case APP_DISCO_EFFECT_THEATER_CHASE:
        return "theater-chase";
    case APP_DISCO_EFFECT_FIREWORKS:
        return "fireworks";
    case APP_DISCO_EFFECT_COUNT:
        break;
    }
    return "rainbow";
}

static bool disco_effect_from_name(const char *name, app_disco_effect_t *effect)
{
    for (app_disco_effect_t candidate = APP_DISCO_EFFECT_RAINBOW;
         candidate < APP_DISCO_EFFECT_COUNT; candidate++) {
        if (strcmp(name, disco_effect_name(candidate)) == 0) {
            *effect = candidate;
            return true;
        }
    }
    return false;
}

static bool manual_light_from_name(const char *name, app_manual_light_t *light,
                                   const char **label)
{
    if (strcmp(name, "red") == 0) {
        *light = APP_MANUAL_LIGHT_RED;
        *label = "Rot";
    } else if (strcmp(name, "yellow") == 0) {
        *light = APP_MANUAL_LIGHT_YELLOW;
        *label = "Gelb";
    } else if (strcmp(name, "green") == 0) {
        *light = APP_MANUAL_LIGHT_GREEN;
        *label = "Grün";
    } else {
        return false;
    }
    return true;
}

static bool color_from_hex_string(const char *value, uint8_t *red, uint8_t *green,
                                  uint8_t *blue)
{
    if (value == NULL || strlen(value) != 7 || value[0] != '#') {
        return false;
    }

    char *end = NULL;
    unsigned long color = strtoul(value + 1, &end, 16);
    if (end != value + 7 || color > 0xFFFFFFUL) {
        return false;
    }

    *red = (uint8_t) (color >> 16);
    *green = (uint8_t) (color >> 8);
    *blue = (uint8_t) color;
    return true;
}

static esp_err_t status_manual_light_handler(httpd_req_t *request)
{
    if (s_manual_light_handler == NULL || s_control_mode_state_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Manuelle Ampelsteuerung ist nicht verfügbar.");
    }
    if (s_control_mode_state_handler() != APP_CONTROL_MODE_MANUAL) {
        return send_status_error(request, "409 Conflict",
                                 "Bitte zuerst den manuellen Modus aktivieren.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültige LED-Steuerung.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "LED-Steuerung konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *light_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "light");
    cJSON *enabled = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "enabled");
    if (!cJSON_IsString(light_name) || light_name->valuestring == NULL || !cJSON_IsBool(enabled)) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request", "Ungültige LED-Steuerung.");
    }

    app_manual_light_t light;
    const char *light_label;
    if (!manual_light_from_name(light_name->valuestring, &light, &light_label)) {
        cJSON_Delete(json);
        return send_status_error(request, "400 Bad Request", "Unbekannte LED.");
    }
    bool light_enabled = cJSON_IsTrue(enabled);
    cJSON_Delete(json);

    ESP_LOGI(HTTP_TAG, "POST /api/light: %s %s", light_label,
             light_enabled ? "ein" : "aus");
    s_manual_light_handler(light, light_enabled);
    char response[48];
    snprintf(response, sizeof(response), "%s ist jetzt %s.", light_label,
             light_enabled ? "eingeschaltet" : "ausgeschaltet");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t status_manual_light_color_handler(httpd_req_t *request)
{
    if (s_control_mode_state_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Manuelle Ampelsteuerung ist nicht verfügbar.");
    }
    if (s_control_mode_state_handler() != APP_CONTROL_MODE_MANUAL) {
        return send_status_error(request, "409 Conflict",
                                 "Bitte zuerst den manuellen Modus aktivieren.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültige LED-Farbe.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "LED-Farbe konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *light_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "light");
    cJSON *color_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "color");
    if (!cJSON_IsString(light_name) || light_name->valuestring == NULL ||
        !cJSON_IsString(color_name) || color_name->valuestring == NULL) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request", "Ungültige LED-Farbe.");
    }

    app_manual_light_t light;
    const char *light_label;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    bool valid = manual_light_from_name(light_name->valuestring, &light, &light_label) &&
                 color_from_hex_string(color_name->valuestring, &red, &green, &blue);
    cJSON_Delete(json);
    if (!valid) {
        return send_status_error(request, "400 Bad Request", "Ungültige LED-Farbe.");
    }

    ESP_LOGI(HTTP_TAG, "POST /api/light-color: %s #%02x%02x%02x", light_label,
             red, green, blue);
    traffic_light_set_manual_color(light, red, green, blue);
    char response[64];
    snprintf(response, sizeof(response), "Farbe für %s gesetzt.", light_label);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t status_manual_lights_state_handler(httpd_req_t *request)
{
    ESP_LOGI(HTTP_TAG, "GET /api/lights");
    if (s_manual_light_state_handler == NULL || s_control_mode_state_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Manuelle Ampelsteuerung ist nicht verfügbar.");
    }

    app_control_mode_t mode = s_control_mode_state_handler();
    const char *disco_effect = s_disco_effect_state_handler == NULL ? "rainbow" :
                               disco_effect_name(s_disco_effect_state_handler());
    uint8_t brightness = s_light_brightness_state_handler == NULL ?
                         APP_LIGHT_BRIGHTNESS_DEFAULT_PERCENT :
                         s_light_brightness_state_handler();
    uint8_t red_red;
    uint8_t red_green;
    uint8_t red_blue;
    uint8_t yellow_red;
    uint8_t yellow_green;
    uint8_t yellow_blue;
    uint8_t green_red;
    uint8_t green_green;
    uint8_t green_blue;
    traffic_light_manual_color(APP_MANUAL_LIGHT_RED, &red_red, &red_green, &red_blue);
    traffic_light_manual_color(APP_MANUAL_LIGHT_YELLOW, &yellow_red, &yellow_green,
                               &yellow_blue);
    traffic_light_manual_color(APP_MANUAL_LIGHT_GREEN, &green_red, &green_green,
                               &green_blue);
    char response[384];
    int response_length = snprintf(response, sizeof(response),
                                   "{\"red\":%s,\"yellow\":%s,\"green\":%s,\"mode\":\"%s\","
                                   "\"pulsing\":%s,\"grey\":%s,"
                                   "\"disco_effect\":\"%s\",\"brightness\":%u,"
                                   "\"red_color\":\"#%02x%02x%02x\","
                                   "\"yellow_color\":\"#%02x%02x%02x\","
                                   "\"green_color\":\"#%02x%02x%02x\"}",
                                   s_manual_light_state_handler(APP_MANUAL_LIGHT_RED) ? "true" : "false",
                                   s_manual_light_state_handler(APP_MANUAL_LIGHT_YELLOW) ? "true" : "false",
                                   s_manual_light_state_handler(APP_MANUAL_LIGHT_GREEN) ? "true" : "false",
                                   control_mode_name(mode),
                                   traffic_light_is_pulsing() ? "true" : "false",
                                   traffic_light_is_grey() ? "true" : "false",
                                   disco_effect, (unsigned) brightness,
                                   (unsigned) red_red, (unsigned) red_green, (unsigned) red_blue,
                                   (unsigned) yellow_red, (unsigned) yellow_green,
                                   (unsigned) yellow_blue, (unsigned) green_red,
                                   (unsigned) green_green, (unsigned) green_blue);
    if (response_length < 0 || response_length >= sizeof(response)) {
        return send_status_error(request, "500 Internal Server Error",
                                 "LED-Zustand konnte nicht gelesen werden.");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, response, response_length);
    ESP_LOGI(HTTP_TAG, "GET /api/lights abgeschlossen: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t status_disco_effect_handler(httpd_req_t *request)
{
    if (s_disco_effect_handler == NULL || s_control_mode_state_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Disco-Steuerung ist nicht verfuegbar.");
    }
    if (s_control_mode_state_handler() != APP_CONTROL_MODE_DISCO) {
        return send_status_error(request, "409 Conflict",
                                 "Bitte zuerst den Disco-Modus aktivieren.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungueltige Disco-Animation.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Disco-Animation konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *effect_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "effect");
    app_disco_effect_t effect;
    if (!cJSON_IsString(effect_name) || effect_name->valuestring == NULL ||
        !disco_effect_from_name(effect_name->valuestring, &effect)) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request", "Unbekannte Disco-Animation.");
    }

    cJSON_Delete(json);
    s_disco_effect_handler(effect);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, "Disco-Animation aktiviert.");
}

static esp_err_t status_brightness_handler(httpd_req_t *request)
{
    if (s_light_brightness_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Helligkeitssteuerung ist nicht verfuegbar.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungueltige Helligkeit.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Helligkeit konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *brightness = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "brightness");
    if (!cJSON_IsNumber(brightness) || brightness->valuedouble != brightness->valueint ||
        brightness->valueint < 1 || brightness->valueint > 100) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request",
                                 "Helligkeit muss eine ganze Zahl von 1 bis 100 sein.");
    }

    uint8_t percent = (uint8_t) brightness->valueint;
    cJSON_Delete(json);
    esp_err_t err = app_config_save_light_brightness(percent);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Helligkeit konnte nicht gespeichert werden: %s", esp_err_to_name(err));
        return send_status_error(request, "500 Internal Server Error",
                                 "Helligkeit konnte nicht gespeichert werden.");
    }

    s_status_config.light_brightness_percent = percent;
    s_light_brightness_handler(percent);
    char response[48];
    snprintf(response, sizeof(response), "Helligkeit auf %u %% gesetzt.", percent);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t status_control_mode_handler(httpd_req_t *request)
{
    if (s_control_mode_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "Betriebsart kann nicht gesetzt werden.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request",
                                 "Ungültige Betriebsart.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "Betriebsart konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *mode_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "mode");
    app_control_mode_t mode;
    if (!cJSON_IsString(mode_name) || mode_name->valuestring == NULL ||
        !control_mode_from_name(mode_name->valuestring, &mode)) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request",
                                 "Betriebsart muss auto, manual, disco oder api sein.");
    }

    cJSON_Delete(json);
    ESP_LOGI(HTTP_TAG, "POST /api/mode: %s", control_mode_name(mode));
    int64_t mode_start_us = esp_timer_get_time();
    s_control_mode_handler(mode);
    ESP_LOGI(HTTP_TAG, "POST /api/mode nach %lld ms angewendet",
             (long long) ((esp_timer_get_time() - mode_start_us) / 1000));
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    if (mode == APP_CONTROL_MODE_AUTO) {
        return httpd_resp_sendstr(request, "Jenkins-Betriebsart ist aktiv.");
    }
    if (mode == APP_CONTROL_MODE_DISCO) {
        return httpd_resp_sendstr(request, "Disco-Modus ist aktiv. Jenkins wird ignoriert.");
    }
    return httpd_resp_sendstr(request, "Betriebsart ist aktiv. Jenkins wird ignoriert.");
}

static esp_err_t status_api_status_handler(httpd_req_t *request)
{
    if (s_api_status_handler == NULL || s_control_mode_state_handler == NULL) {
        return send_status_error(request, "503 Service Unavailable",
                                 "REST-API-Steuerung ist nicht verfügbar.");
    }
    if (s_control_mode_state_handler() != APP_CONTROL_MODE_API) {
        return send_status_error(request, "409 Conflict",
                                 "Bitte zuerst den API-Modus aktivieren.");
    }
    if (request->content_len == 0 || request->content_len >= STATUS_REQUEST_MAX_LENGTH) {
        return send_status_error(request, "400 Bad Request", "Ungültiger API-Status.");
    }

    char request_body[STATUS_REQUEST_MAX_LENGTH];
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, request_body + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return send_status_error(request, "400 Bad Request",
                                     "API-Status konnte nicht gelesen werden.");
        }
        received += result;
    }
    request_body[received] = '\0';

    cJSON *json = cJSON_Parse(request_body);
    cJSON *status_name = json == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(json, "status");
    app_light_status_t status;
    if (!cJSON_IsString(status_name) || status_name->valuestring == NULL) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return send_status_error(request, "400 Bad Request",
                                 "Status muss off, red, yellow oder green sein.");
    }
    if (strcmp(status_name->valuestring, "off") == 0) {
        status = APP_LIGHT_STATUS_OFF;
    } else if (strcmp(status_name->valuestring, "red") == 0) {
        status = APP_LIGHT_STATUS_RED;
    } else if (strcmp(status_name->valuestring, "yellow") == 0) {
        status = APP_LIGHT_STATUS_YELLOW;
    } else if (strcmp(status_name->valuestring, "green") == 0) {
        status = APP_LIGHT_STATUS_GREEN;
    } else {
        cJSON_Delete(json);
        return send_status_error(request, "400 Bad Request",
                                 "Status muss off, red, yellow oder green sein.");
    }

    ESP_LOGI(HTTP_TAG, "POST /api/status: %s", status_name->valuestring);
    cJSON_Delete(json);
    s_api_status_handler(status);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, "{\"mode\":\"api\",\"status\":\"updated\"}");
}

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32S3:
        return "ESP32-S3";
    default:
        return "Unbekannt";
    }
}

static esp_err_t status_device_info_handler(httpd_req_t *request)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t cpu_hz = 0;
    uint32_t apb_hz = 0;
    uint32_t xtal_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                  &cpu_hz);
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                  &apb_hz);
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_XTAL, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                  &xtal_hz);
    const esp_app_desc_t *app = esp_app_get_description();
    uint32_t flash_size = 0;
    bool flash_known = esp_flash_get_physical_size(NULL, &flash_size) == ESP_OK;
    wifi_ap_record_t access_point;
    bool wifi_connected = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    char wifi_mac[18] = "";
    char wifi_ipv4[16] = "";
    char wifi_ipv6_global[40] = "";
    char wifi_ipv6_linklocal[40] = "";
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(wifi_mac, sizeof(wifi_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            snprintf(wifi_ipv4, sizeof(wifi_ipv4), IPSTR, IP2STR(&ip_info.ip));
        }
        esp_ip6_addr_t ip6;
        if (esp_netif_get_ip6_global(sta_netif, &ip6) == ESP_OK) {
            snprintf(wifi_ipv6_global, sizeof(wifi_ipv6_global), IPV6STR, IPV62STR(ip6));
        }
        if (esp_netif_get_ip6_linklocal(sta_netif, &ip6) == ESP_OK) {
            snprintf(wifi_ipv6_linklocal, sizeof(wifi_ipv6_linklocal), IPV6STR, IPV62STR(ip6));
        }
    }

    // Capture the heap before allocating the JSON response itself.
    size_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    size_t internal_total = heap_caps_get_total_size(internal_caps);
    size_t internal_free = heap_caps_get_free_size(internal_caps);
    size_t internal_min = heap_caps_get_minimum_free_size(internal_caps);
    size_t internal_largest = heap_caps_get_largest_free_block(internal_caps);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint64_t uptime_seconds = (uint64_t) esp_timer_get_time() / 1000000;

    cJSON *info = cJSON_CreateObject();
    if (info == NULL ||
        cJSON_AddStringToObject(info, "chip", chip_model_name(chip_info.model)) == NULL ||
        cJSON_AddNumberToObject(info, "chip_model_id", chip_info.model) == NULL ||
        cJSON_AddNumberToObject(info, "chip_revision_major", chip_info.revision / 100) == NULL ||
        cJSON_AddNumberToObject(info, "chip_revision_minor", chip_info.revision % 100) == NULL ||
        cJSON_AddNumberToObject(info, "cores", chip_info.cores) == NULL ||
        cJSON_AddNumberToObject(info, "cpu_hz", cpu_hz) == NULL ||
        cJSON_AddNumberToObject(info, "cpu_max_hz",
                                chip_info.model == CHIP_ESP32S3 ? 240000000 : 0) == NULL ||
        cJSON_AddNumberToObject(info, "apb_hz", apb_hz) == NULL ||
        cJSON_AddNumberToObject(info, "xtal_hz", xtal_hz) == NULL ||
        cJSON_AddNumberToObject(info, "flash_bytes", flash_known ? flash_size : 0) == NULL ||
        cJSON_AddStringToObject(info, "hostname", s_light_hostname) == NULL ||
        cJSON_AddStringToObject(info, "wifi_mac", wifi_mac) == NULL ||
        cJSON_AddStringToObject(info, "wifi_ipv4", wifi_ipv4) == NULL ||
        cJSON_AddStringToObject(info, "wifi_ipv6_global", wifi_ipv6_global) == NULL ||
        cJSON_AddStringToObject(info, "wifi_ipv6_linklocal", wifi_ipv6_linklocal) == NULL ||
        cJSON_AddStringToObject(info, "firmware_version", app->version) == NULL ||
        cJSON_AddStringToObject(info, "build_date", app->date) == NULL ||
        cJSON_AddStringToObject(info, "build_time", app->time) == NULL ||
        cJSON_AddStringToObject(info, "idf_version", app->idf_ver) == NULL ||
        cJSON_AddNumberToObject(info, "uptime_seconds", (double) uptime_seconds) == NULL ||
        cJSON_AddNumberToObject(info, "internal_total_bytes", internal_total) == NULL ||
        cJSON_AddNumberToObject(info, "internal_free_bytes", internal_free) == NULL ||
        cJSON_AddNumberToObject(info, "internal_min_bytes", internal_min) == NULL ||
        cJSON_AddNumberToObject(info, "internal_largest_bytes", internal_largest) == NULL ||
        cJSON_AddNumberToObject(info, "psram_free_bytes", psram_free) == NULL ||
        cJSON_AddNumberToObject(info, "psram_total_bytes", psram_total) == NULL ||
        (wifi_connected && (cJSON_AddNumberToObject(info, "wifi_rssi_dbm", access_point.rssi) == NULL ||
                            cJSON_AddNumberToObject(info, "wifi_channel", access_point.primary) == NULL))) {
        cJSON_Delete(info);
        return send_status_error(request, "500 Internal Server Error",
                                 "Geraetedaten konnten nicht erstellt werden.");
    }

    char *body = cJSON_PrintUnformatted(info);
    cJSON_Delete(info);
    if (body == NULL) {
        return send_status_error(request, "500 Internal Server Error",
                                 "Geraetedaten konnten nicht erstellt werden.");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t status_page_handler(httpd_req_t *request)
{
    int64_t request_start_us = esp_timer_get_time();
    ESP_LOGI(HTTP_TAG, "GET / startet");
    static const char page[] =
        "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>CI-Lights</title>"
        "<link rel=\"icon\" href=\"/favicon.ico\" sizes=\"any\">"
        "<style>:root{--navy:#1f2937;--blue:#2563eb;--green:#16a34a;--ink:#102a43;--muted:#5c7080;--surface:#fff}"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:linear-gradient(145deg,#eef5fb,#f7fbf3);color:var(--ink);font-family:system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
        ".page{position:relative;max-width:48rem;margin:auto;padding:clamp(1rem,4vw,2rem)}.brand{position:relative;display:flex;justify-content:center;align-items:center;height:clamp(6rem,16.666vh,10rem);margin:0 0 1rem;border:2px dashed #b8c9d6;border-radius:1rem;background:#e8f1f8;overflow:hidden}.brand.has-banner{border-style:solid;background:#fff}.brand img{display:none;max-width:100%;max-height:100%;width:auto;height:auto;object-fit:contain}.brand.has-banner img{display:block}.banner-upload{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:.35rem;width:100%;min-height:0;margin:0;border:0;border-radius:0;background:transparent;color:var(--navy);font:700 .95rem inherit;box-shadow:none;cursor:pointer}.banner-upload svg{width:2rem;height:2rem;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.brand.has-banner .banner-upload{opacity:0;background:rgba(31,41,55,.78);color:#fff;transition:opacity .15s}.brand.has-banner .banner-upload:hover,.brand.has-banner .banner-upload:focus-visible{opacity:1}.banner-upload:focus-visible{outline:3px solid var(--blue);outline-offset:-3px}.language-switch{display:flex;align-items:center;gap:.2rem;width:max-content;margin:0 0 .75rem auto;padding:.2rem;border:1px solid #dbe7ef;border-radius:.7rem;background:#fff}.language-switch button{min-height:0;padding:.25rem .35rem;border-radius:.4rem;background:transparent;box-shadow:none;line-height:1}.language-switch .flag{display:block;width:1.8rem;height:1.1rem;border-radius:.12rem;box-shadow:0 0 0 1px rgba(16,42,67,.18)}.language-switch button[aria-pressed=\"true\"]{background:#dcebf9;outline:2px solid var(--navy)}"
        ".card{background:var(--surface);border:1px solid #dbe7ef;border-top:0;border-radius:0 0 1.25rem 1.25rem;padding:clamp(1.25rem,5vw,2rem);box-shadow:0 1rem 2.5rem rgba(31,41,55,.12)}"
        ".eyebrow{margin:0;color:var(--blue);font-size:.78rem;font-weight:800;letter-spacing:.09em;text-transform:uppercase}h1{margin:.3rem 0 1rem;padding:.7rem 1rem;border:1px solid #dbe7ef;border-radius:1rem;background:#fff;color:var(--navy);font-size:clamp(1.7rem,7vw,2.35rem);text-align:center;box-shadow:0 .45rem 1.2rem rgba(31,41,55,.08)}h2{margin-top:0;color:var(--navy);font-size:1.25rem}p{line-height:1.55}"
        "label{display:block;margin-top:1rem;font-weight:700}input,select{width:100%;margin-top:.4rem;border:1px solid #b8c9d6;border-radius:.7rem;padding:.85rem;font:inherit;background:#fff}input:focus,select:focus{outline:3px solid #b9d7fa;border-color:var(--blue)}"
        "button{min-height:2.8rem;border:0;border-radius:.7rem;background:var(--navy);color:#fff;padding:.75rem 1rem;font:700 .96rem inherit;box-shadow:0 .35rem .8rem rgba(31,41,55,.18)}button:active{transform:translateY(1px)}button:disabled{cursor:not-allowed;opacity:.48}form button{width:100%;margin-top:1.4rem}"
        ".job-picker{display:flex;align-items:flex-end;gap:.55rem}.job-picker label{flex:1;min-width:0}.job-refresh{display:grid;place-items:center;flex:0 0 2.9rem;width:2.9rem;height:2.9rem;min-height:0;margin:0 0 .05rem;padding:0;border:1px solid #b8d7fa;background:#e8f1f8;color:var(--navy);box-shadow:none;cursor:pointer}.job-refresh:hover{background:#d6e9fb}.job-refresh:focus-visible{outline:3px solid var(--blue);outline-offset:2px}.job-refresh svg{width:1.25rem;height:1.25rem;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}.job-list-note{margin:.65rem 0 0;color:var(--muted);font-size:.85rem}"
        ".wifi-settings{margin-top:2rem;padding-top:1.4rem;border-top:1px solid #dbe7ef}.wifi-settings h2{margin-bottom:.35rem}.wifi-name-field{position:relative}.wifi-name-field input{padding-right:3.2rem}.wifi-name-field button{position:absolute;right:.3rem;bottom:.3rem;width:2.45rem;min-height:0;margin:0;padding:.45rem;border-radius:.5rem;font-size:1.35rem;line-height:1}.saved-wifi{display:grid;gap:.45rem;margin-top:1rem}.wifi-entry{display:flex;align-items:center;justify-content:space-between;gap:.7rem;padding:.6rem .75rem;border:1px solid #dbe7ef;border-radius:.65rem;background:#f5f9fc}.wifi-entry span{min-width:0;overflow-wrap:anywhere}.wifi-entry button{min-height:2rem;flex:0 0 auto;padding:.35rem .6rem;background:#a11227;font-size:.8rem}"
        ".site-title-row{padding:0}.site-title-row button,.site-title-row input{display:block;width:100%;min-height:0;margin:0;padding:.7rem 1rem;border:0;border-radius:1rem;background:transparent;color:inherit;font:inherit;line-height:inherit;text-align:center;box-shadow:none}.site-title-row button{cursor:text}.site-title-row button:hover{background:#e8f1f8}.site-title-row button:disabled{opacity:1;cursor:wait}.site-title-row button:focus-visible,.site-title-row input:focus{outline:3px solid var(--blue);outline-offset:-3px}.site-title-row button[hidden],.site-title-row input[hidden]{display:none}"
        ".branding-settings{margin-top:2rem;padding-top:1.4rem;border-top:1px solid #dbe7ef}.branding-item{margin-top:1rem;padding:1rem;border:1px solid #dbe7ef;border-radius:.75rem;background:#f5f9fc}.branding-item label{margin-top:0}.branding-item .hint{margin:.55rem 0 0}"
        ".tabs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:.25rem;max-width:100%%;margin:1rem 0 0;padding:0 .75rem;border-bottom:1px solid #dbe7ef;position:relative;z-index:1}.tabs a{min-width:0;margin:0 0 -1px;padding:.7rem .45rem;border:1px solid transparent;border-bottom:0;border-radius:.75rem .75rem 0 0;color:var(--muted);font-size:.9rem;font-weight:750;line-height:1.2;text-align:center;text-decoration:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.tabs a[aria-selected=\"true\"]{border-color:#dbe7ef;border-bottom:1px solid #fff;background:#fff;color:var(--navy);box-shadow:0 -.15rem .5rem rgba(31,41,55,.07)}.tabs a:focus-visible{outline:3px solid #b9d7fa;outline-offset:-3px}.tab{display:none}.tab.active{display:block}"
        ".portal-layout{display:grid;grid-template-columns:minmax(0,1fr) 5.4rem;gap:1.15rem;align-items:stretch}.portal-layout>div{grid-column:1;grid-row:1}.traffic-light{display:flex;grid-column:2;grid-row:1;min-height:100%;flex-direction:column;align-items:center;filter:drop-shadow(0 .35rem .55rem rgba(31,41,55,.18))}.traffic-light-top{display:block;width:100%;height:auto;flex:0 0 auto;margin-bottom:-.55rem;position:relative;z-index:1}.traffic-light-middle{width:1.15rem;min-height:1rem;flex:1 1 auto;background:linear-gradient(90deg,#1f2937 0,#1f2937 34%,#2563eb 34%,#2563eb 55%,#1f2937 55%)}.traffic-light-base{width:100%;height:1rem;flex:0 0 auto;background:linear-gradient(145deg,#374151,#111827);clip-path:polygon(10% 0,90% 0,100% 100%,0 100%)}.poll-settings{margin-top:2rem;padding-top:1.4rem;border-top:1px solid #dbe7ef}.poll-settings h2{margin-bottom:.35rem}.manual-override{width:100%;margin:.75rem 0 .25rem;background:#e8f1f8;color:var(--navy);box-shadow:none}.manual-override[aria-pressed=\"true\"]{background:var(--green);color:#163100}.manual-controls,.manual-color-controls{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:.6rem;margin-top:1rem}.manual-controls button{margin:0;background:#e8f1f8;color:var(--navy);box-shadow:none}.manual-controls button[aria-pressed=\"true\"]{background:var(--green);color:#163100}.manual-color-controls label{margin:0;color:var(--navy);font-size:.85rem}.manual-color-controls input{height:2.8rem;padding:.2rem}.light-state{margin:.7rem 0 0;color:var(--muted);font-size:.87rem;line-height:1.4}.hint{color:var(--muted);font-size:.93rem}#status{display:none;margin:0 0 .5rem;padding:.85rem 1rem;border:1px solid #c7d9e8;border-left:.35rem solid var(--blue);border-radius:.55rem;background:#f2f8fd;white-space:pre-wrap;color:var(--navy);font-weight:650}#status:not(:empty){display:block}#status[data-error=\"true\"]{border-color:#f1b7be;border-left-color:#c6283b;background:#fff0f1;color:#a11227}"
        ".brightness-control{display:grid;gap:.45rem;margin:0 0 1rem;padding:.8rem 1rem;border:1px solid #dbe7ef;border-radius:.85rem;background:#fff;box-shadow:0 .35rem .8rem rgba(31,41,55,.06)}.brightness-label{margin:0;color:var(--navy);font-size:.95rem}.brightness-row{display:flex;align-items:center;gap:.8rem}.brightness-row input{margin:0}.brightness-row output{min-width:3.5rem;color:var(--navy);font-weight:800;text-align:right}.disco-controls{margin-top:1rem;padding:1rem;border:1px solid #dbe7ef;border-radius:.75rem;background:#f4f8fb}.disco-controls[hidden]{display:none}.disco-controls label{margin-top:0}"
        ".device-footer{margin-top:1rem;padding:1.15rem 1.3rem;border:1px solid #dbe7ef;border-radius:1rem;background:#fff;box-shadow:0 .45rem 1.2rem rgba(31,41,55,.06)}.device-footer summary{cursor:pointer;color:var(--navy);font-size:1.1rem;font-weight:750}.device-footer summary:focus-visible{outline:3px solid #b9d7fa;outline-offset:3px}.device-info{display:grid;grid-template-columns:repeat(auto-fit,minmax(17rem,1fr));gap:.8rem;margin-top:.9rem}.device-group{min-width:0;padding:.85rem 1rem;border:1px solid #dbe7ef;border-radius:.75rem;background:#f5f9fc}.device-group h3{margin:0 0 .6rem;color:var(--navy);font-size:.88rem}.device-metrics{display:grid;gap:.4rem;margin:0}.device-metric{display:flex;justify-content:space-between;align-items:baseline;gap:.7rem;min-width:0;padding-bottom:.35rem;border-bottom:1px solid #e1eaf1}.device-metric:last-child{padding-bottom:0;border-bottom:0}.device-metric dt{color:var(--muted);font-size:.78rem;font-weight:700}.device-metric dd{min-width:0;margin:0;overflow-wrap:anywhere;text-align:right;font-size:.86rem;font-weight:650;font-variant-numeric:tabular-nums}.device-message{margin:0;color:var(--muted);font-size:.9rem}"
        "#toast{position:fixed;z-index:10;top:1rem;right:1rem;width:min(calc(100vw - 2rem),28rem);padding:1rem 1.25rem;border-radius:.85rem;background:#357b12;color:#fff;font-weight:750;line-height:1.4;text-align:center;box-shadow:0 1rem 2.5rem rgba(16,42,67,.3);opacity:0;pointer-events:none;transform:translateY(-.5rem);transition:opacity .2s ease,transform .2s ease}#toast.show{opacity:1;transform:translateY(0)}"
        "#job-loading,#jenkins-loading{position:fixed;z-index:20;inset:0;display:none;align-items:center;justify-content:center;padding:1rem;background:rgba(16,42,67,.42)}#job-loading.show,#jenkins-loading.show{display:flex}.job-loading-card{display:flex;align-items:center;gap:.8rem;padding:1rem 1.25rem;border-radius:.85rem;background:#fff;color:var(--navy);font-weight:750;box-shadow:0 1rem 2.5rem rgba(16,42,67,.3)}.spinner{width:1.7rem;height:1.7rem;flex:0 0 auto;border:.25rem solid #c8d9e8;border-top-color:var(--blue);border-radius:50%;animation:job-spin .8s linear infinite}@keyframes job-spin{to{transform:rotate(360deg)}}"
        ".login-overlay{position:fixed;z-index:100;inset:0;display:flex;align-items:center;justify-content:center;padding:1rem;background:rgba(16,42,67,.55);backdrop-filter:blur(4px)}.login-overlay[hidden]{display:none}.login-card{width:min(100%,27rem);padding:clamp(1.5rem,5vw,2.25rem);border:1px solid #dbe7ef;border-radius:1.25rem;background:#fff;box-shadow:0 1.5rem 3rem rgba(0,32,65,.28)}.login-card h2{margin:.35rem 0 .4rem;font-size:1.6rem}.login-card .hint{margin:0}.login-card button{margin-top:1.25rem}.login-error{min-height:1.3rem;margin:.8rem 0 0;color:#a11227;font-size:.9rem;font-weight:700}"
        "@media(max-width:430px){.page{padding:.75rem}.card{padding:1.1rem;border-radius:0 0 1rem 1rem}.tabs{padding:0 .4rem}.tabs a{padding:.65rem .3rem;font-size:.78rem}.portal-layout{grid-template-columns:minmax(0,1fr) 3.3rem;gap:.75rem}.manual-controls{grid-template-columns:1fr}}</style></head><body>"
        "<div id=\"login-overlay\" class=\"login-overlay\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"login-title\"><form id=\"login-form\" class=\"login-card\"><p class=\"eyebrow\">CI-Lights</p><h2 id=\"login-title\">Verwaltung anmelden</h2><p class=\"hint\">Melde dich mit dem Benutzernamen und Passwort aus der Einrichtung an.</p><label>Benutzername<input id=\"login-username\" name=\"username\" autocomplete=\"username\" required maxlength=\"32\" autofocus></label><label>Passwort<input id=\"login-password\" name=\"password\" type=\"password\" autocomplete=\"current-password\" required></label><p id=\"login-error\" class=\"login-error\" role=\"alert\"></p><button type=\"submit\">Anmelden</button></form></div>"
        "<main id=\"dashboard\" class=\"page\" inert aria-hidden=\"true\">" LANGUAGE_SWITCH_HTML
        "<form id=\"banner-form\" class=\"brand\" data-branding=\"logo\"><img src=\"/logo.svg\" alt=\"Banner\" onload=\"this.parentElement.classList.add('has-banner')\" onerror=\"this.parentElement.classList.remove('has-banner')\"><input id=\"banner-file\" type=\"file\" accept=\".svg,image/svg+xml,.png,image/png\" required hidden><button class=\"banner-upload\" type=\"button\" title=\"Banner hochladen (SVG oder PNG, maximal 32 KiB)\"><svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 16V3m0 0L7 8m5-5 5 5M4 16v4h16v-4\"/></svg><span>Banner hochladen</span></button></form>"
        "<h1 class=\"site-title-row\"><button id=\"site-title-edit\" type=\"button\" aria-label=\"Titel bearbeiten\" title=\"Titel bearbeiten\" aria-controls=\"site-title-input\" aria-expanded=\"false\"><span id=\"site-title\">CI-Lights</span></button><input id=\"site-title-input\" type=\"text\" aria-label=\"Titel bearbeiten\" maxlength=\"64\" hidden></h1><p id=\"status\" role=\"status\" aria-live=\"polite\"></p><nav class=\"tabs\" role=\"tablist\" aria-label=\"Verwaltung\">"
        "<a href=\"#job\" role=\"tab\" data-tab=\"job\" aria-controls=\"tab-job\" aria-selected=\"true\">Job</a>"
        "<a href=\"#jenkins\" role=\"tab\" data-tab=\"jenkins\" aria-controls=\"tab-jenkins\" aria-selected=\"false\">Konfiguration</a>"
        "<a href=\"#manual\" role=\"tab\" data-tab=\"manual\" aria-controls=\"tab-manual\" aria-selected=\"false\">Steuerung</a></nav><section class=\"card\"><div class=\"portal-layout\"><aside class=\"traffic-light\" aria-label=\"CI-Lights\"><object id=\"traffic-light\" class=\"traffic-light-top\" data=\"/traffic-light.svg\" type=\"image/svg+xml\" aria-label=\"Aktueller Ampelzustand\">Ampel</object><div class=\"traffic-light-middle\"></div><div class=\"traffic-light-base\"></div></aside><div>"
        "<section id=\"tab-jenkins\" class=\"tab\" role=\"tabpanel\"><h2>Jenkins-Zugang</h2>"
        "<p class=\"hint\">Zum Ändern alle drei Werte erneut eingeben. Der Token wird nicht angezeigt.</p>"
        "<form id=\"jenkins-form\"><label>Jenkins-URL<input name=\"jenkins_url\" type=\"url\" placeholder=\"https://jenkins.example.de\" required maxlength=\"127\"></label>"
        "<label>Jenkins-Benutzer<input name=\"jenkins_user\" required maxlength=\"63\"></label>"
        "<label>Jenkins-API-Token<input name=\"jenkins_token\" type=\"password\" required maxlength=\"127\"></label>"
        "<button type=\"submit\">Jenkins-Zugang speichern</button></form><section class=\"poll-settings\"><h2>Automatische Abfrage</h2>"
        "<p class=\"hint\">Der Jenkins-Status wird in diesem Abstand aktualisiert. Erlaubt sind ganze Minuten ab 1.</p>"
        "<form id=\"poll-interval-form\"><label>Abfrageintervall in Minuten<input id=\"poll-interval\" name=\"poll_interval_minutes\" type=\"number\" min=\"1\" step=\"1\" inputmode=\"numeric\" required></label>"
        "<button type=\"submit\">Intervall speichern</button></form></section>"
        "<section class=\"wifi-settings\"><h2>Gespeicherte WLANs</h2><p class=\"hint\">Bis zu acht WLANs. Bei voller Liste ersetzt ein neues WLAN das am längsten nicht neu gespeicherte Profil. Beim Start wird zuerst das zuletzt verwendete versucht, danach die anderen. Ist keines erreichbar, startet die WLAN-Einrichtung.</p><div id=\"saved-wifi\" class=\"saved-wifi\"></div>"
        "<form id=\"wifi-form\"><label>WLAN-Name<div class=\"wifi-name-field\"><input id=\"wifi-ssid\" name=\"wifi_ssid\" list=\"wifi-networks\" placeholder=\"WLAN auswählen oder eingeben\" required maxlength=\"32\"><button id=\"wifi-refresh\" type=\"button\" aria-label=\"WLANs aktualisieren\" title=\"WLANs aktualisieren\">&#x21bb;</button></div><datalist id=\"wifi-networks\"></datalist></label>"
        "<label>WLAN-Benutzername (nur WPA2-Enterprise)<input name=\"wifi_username\" maxlength=\"127\"></label><label>WLAN-Passwort (bei offenen WLANs leer lassen)<input name=\"wifi_password\" type=\"password\" maxlength=\"63\"></label>"
        "<p class=\"hint\">Bei geschützten WLANs das Passwort erneut eingeben. Ein bereits gespeicherter WLAN-Name wird aktualisiert.</p><button type=\"submit\">WLAN speichern und neu starten</button></form><p id=\"wifi-scan-status\" class=\"hint\" role=\"status\"></p></section>"
        "<section class=\"branding-settings\"><h2>Branding</h2><p class=\"hint\">Das Banner oben kannst du direkt anklicken und hochladen. Das Favicon wird hier hochgeladen. Beide Dateien bleiben nach einem Neustart im Flash gespeichert.</p><form class=\"branding-item\" data-branding=\"favicon\"><label>Favicon (ICO oder PNG, maximal 32 KiB)<input type=\"file\" accept=\".ico,image/x-icon,image/vnd.microsoft.icon,.png,image/png\" required></label><p class=\"hint\" data-branding-state=\"favicon\" role=\"status\">Noch nicht hochgeladen</p><button type=\"submit\">Favicon hochladen</button></form></section></section>"
        "<section id=\"tab-manual\" class=\"tab\" role=\"tabpanel\"><h2>Ampelsteuerung</h2>"
        "<p class=\"hint\">Wähle, wer die Ampel steuert. Manuell und API setzen Jenkins vollständig aus.</p>"
        "<section class=\"brightness-control\" aria-label=\"LED-Helligkeit\"><label class=\"brightness-label\" for=\"brightness\">LED-Helligkeit</label><div class=\"brightness-row\"><input id=\"brightness\" type=\"range\" min=\"1\" max=\"100\" step=\"1\" value=\"50\"><output id=\"brightness-value\" for=\"brightness\">50 %</output></div></section><label>Betriebsart<select id=\"control-mode\"><option value=\"auto\">Auto (Jenkins)</option><option value=\"manual\">Manuell</option><option value=\"disco\">Disco</option><option value=\"api\">REST API</option></select></label><p id=\"mode-hint\" class=\"hint\"></p><section id=\"disco-controls\" class=\"disco-controls\" hidden><label for=\"disco-effect\">Disco-Animation<select id=\"disco-effect\" disabled><option value=\"rainbow\">Rainbow</option><option value=\"colorloop\">Colorloop</option><option value=\"chase\">Chase</option><option value=\"rainbow-chase\">Rainbow Chase</option><option value=\"blink\">Blink</option><option value=\"breathe\">Breathe</option><option value=\"twinkle\">Twinkle</option><option value=\"scan\">Scan</option><option value=\"theater-chase\">Theater Chase</option><option value=\"fireworks\">Fireworks</option></select></label><p class=\"hint\">Zehn kompakte Effekte, angelehnt an WLED.</p></section><div class=\"manual-controls\">"
        "<button type=\"button\" data-light=\"red\" data-enabled=\"false\" aria-pressed=\"false\" disabled>Rot: Aus</button>"
        "<button type=\"button\" data-light=\"yellow\" data-enabled=\"false\" aria-pressed=\"false\" disabled>Gelb: Aus</button>"
        "<button type=\"button\" data-light=\"green\" data-enabled=\"false\" aria-pressed=\"false\" disabled>Grün: Aus</button></div><div class=\"manual-color-controls\" aria-label=\"LED-Farben\"><label>Rot<input type=\"color\" data-light-color=\"red\" value=\"#ff0000\" disabled></label><label>Gelb<input type=\"color\" data-light-color=\"yellow\" value=\"#ffff00\" disabled></label><label>Grün<input type=\"color\" data-light-color=\"green\" value=\"#00ff00\" disabled></label></div><p id=\"light-state\" class=\"light-state\"></p><p class=\"hint\">Im API-Modus: <code>POST /api/status</code> mit <code>{\"status\":\"green\"}</code>.</p></section>"
        "<section id=\"tab-job\" class=\"tab active\" role=\"tabpanel\"><h2>Jenkins-Job</h2>"
        "<p>Wähle den Job, dessen Status die Ampel anzeigen soll.</p>"
        "<form id=\"job-form\"><div class=\"job-picker\"><label>Jenkins-Job<select id=\"job\" required disabled></select></label><button id=\"refresh-jobs\" class=\"job-refresh\" type=\"button\" aria-label=\"Jobliste aktualisieren\" title=\"Jobliste aktualisieren\"><svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M20 11a8 8 0 1 0-2.3 6.7M20 4v7h-7\"/></svg></button></div><p id=\"job-list-note\" class=\"job-list-note\" role=\"status\">Liste noch nicht geladen.</p>"
        "<button id=\"show-job\" type=\"submit\" disabled>Job anzeigen</button></form></section></div></div></section><details id=\"board-details\" class=\"device-footer\"><summary>Mikrocontroller</summary><div id=\"device-info\" class=\"device-info\"><p class=\"device-message\">Gerätedaten werden geladen ...</p></div></details></main><div id=\"toast\" role=\"status\" aria-live=\"polite\"></div><div id=\"job-loading\" role=\"dialog\" aria-modal=\"true\" aria-label=\"Jobliste wird geladen\" aria-hidden=\"true\"><div class=\"job-loading-card\"><span class=\"spinner\" aria-hidden=\"true\"></span><span>Jobliste wird geladen ...</span></div></div><div id=\"jenkins-loading\" role=\"dialog\" aria-modal=\"true\" aria-label=\"Jenkins-Status wird abgefragt\" aria-hidden=\"true\"><div class=\"job-loading-card\"><span class=\"spinner\" aria-hidden=\"true\"></span><span>Jenkins-Status wird abgefragt ...</span></div></div>"
        "<script src=\"/localization.js\"></script><script>const loginOverlay=document.querySelector('#login-overlay'),loginForm=document.querySelector('#login-form'),loginUsername=document.querySelector('#login-username'),loginPassword=document.querySelector('#login-password'),loginError=document.querySelector('#login-error'),dashboard=document.querySelector('#dashboard'),nativeFetch=window.fetch.bind(window);let adminAuthorization='',dashboardStarted=false;"
        "function showLogin(){adminAuthorization='';dashboard.inert=true;dashboard.setAttribute('aria-hidden','true');loginOverlay.hidden=false;loginPassword.value='';loginUsername.focus()}"
        "window.fetch=(input,options={})=>{if(typeof input!=='string'||!input.startsWith('/api/')||input==='/api/login')return nativeFetch(input,options);if(!adminAuthorization)return Promise.reject(new Error('Bitte anmelden.'));const headers=new Headers(options.headers);headers.set('Authorization',adminAuthorization);headers.set('X-Admin-UI','1');return nativeFetch(input,{...options,headers}).then(response=>{if(response.status===401){showLogin();throw new Error('Bitte erneut anmelden.')}return response})};"
        "loginForm.addEventListener('submit',async event=>{event.preventDefault();const username=loginUsername.value.trim(),password=loginPassword.value,button=loginForm.querySelector('button');button.disabled=true;loginError.textContent='';try{const response=await nativeFetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username,password})});if(!response.ok)throw new Error(await response.text());const bytes=new TextEncoder().encode(username+':'+password);adminAuthorization='Basic '+btoa(String.fromCharCode(...bytes));loginPassword.value='';loginOverlay.hidden=true;dashboard.inert=false;dashboard.setAttribute('aria-hidden','false');if(!dashboardStarted)startDashboard();else refreshDashboard()}catch(error){loginError.textContent=error.message}finally{button.disabled=false}});loginUsername.focus();"
        "const select=document.querySelector('#job'),refreshJobs=document.querySelector('#refresh-jobs'),showJob=document.querySelector('#show-job'),jobListNote=document.querySelector('#job-list-note'),status=document.querySelector('#status'),pollInterval=document.querySelector('#poll-interval'),controlMode=document.querySelector('#control-mode'),modeHint=document.querySelector('#mode-hint'),discoControls=document.querySelector('#disco-controls'),discoEffect=document.querySelector('#disco-effect'),brightness=document.querySelector('#brightness'),brightnessValue=document.querySelector('#brightness-value'),toast=document.querySelector('#toast'),jobLoading=document.querySelector('#job-loading'),jenkinsLoading=document.querySelector('#jenkins-loading');"
        "const wifiForm=document.querySelector('#wifi-form'),wifiSsid=document.querySelector('#wifi-ssid'),wifiNetworks=document.querySelector('#wifi-networks'),wifiRefresh=document.querySelector('#wifi-refresh'),wifiScanStatus=document.querySelector('#wifi-scan-status'),savedWifi=document.querySelector('#saved-wifi');let wifiData=null,wifiTabLoaded=false,wifiTabLoading=false,wifiScanSource='';"
        "const brandingForms=[...document.querySelectorAll('[data-branding]')];let brandingData=null;"
        "const siteTitleHeading=document.querySelector('#site-title'),siteTitleButton=document.querySelector('#site-title-edit'),siteTitleInput=document.querySelector('#site-title-input');let savedSiteTitle='CI-Lights';"
        "let statusSource='';function showStatus(message,isError=false){statusSource=message;status.textContent=message.startsWith('Fehler: ')?uiI18n.t('Fehler: ')+uiI18n.response(message.slice(8)):uiI18n.response(message);status.dataset.error=isError?'true':'false'}"
        "function showWifiScanStatus(message){wifiScanSource=message;wifiScanStatus.textContent=message.startsWith('Fehler: ')?uiI18n.t('Fehler: ')+uiI18n.response(message.slice(8)):uiI18n.response(message)}"
        "function renderSavedWifi(){if(!wifiData)return;savedWifi.replaceChildren(...wifiData.networks.map(network=>{const row=document.createElement('div'),label=document.createElement('span'),remove=document.createElement('button');row.className='wifi-entry';const tags=[];if(network.ssid===wifiData.connected_ssid)tags.push(uiI18n.t('Verbunden'));if(network.last_used)tags.push(uiI18n.t('Zuletzt verwendet'));label.textContent=network.ssid+(tags.length?' ('+tags.join(', ')+')':'');remove.type='button';remove.textContent=uiI18n.t('Entfernen');remove.disabled=wifiData.networks.length===1;remove.addEventListener('click',async()=>{remove.disabled=true;try{const r=await fetch('/api/wifi',{method:'DELETE',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:network.ssid})});if(await showSaveResult(r))await loadSavedWifi();else remove.disabled=false}catch(error){showStatus('Fehler: '+error.message,true);remove.disabled=false}});row.append(label,remove);return row}))}"
        "async function loadSavedWifi(){const r=await fetch('/api/wifi',{cache:'no-store'});if(!r.ok)throw new Error(await r.text());wifiData=await r.json();if(!wifiTabLoaded)wifiSsid.value=wifiData.connected_ssid;renderSavedWifi()}"
        "async function loadWifiNetworks(){if(wifiRefresh.disabled)return;wifiRefresh.disabled=true;wifiNetworks.replaceChildren();showWifiScanStatus('Suche nach WLANs ...');try{const r=await fetch('/api/wifi-networks');if(!r.ok)throw new Error(await r.text());const data=await r.json();for(const name of data.networks)wifiNetworks.append(new Option(name,name));showWifiScanStatus(data.networks.length?'':'Keine WLANs gefunden. Du kannst den Namen auch manuell eingeben.')}finally{wifiRefresh.disabled=false}}"
        "function renderBranding(){if(!brandingData)return;const item=brandingData.favicon,state=document.querySelector('[data-branding-state=favicon]');state.textContent=item.uploaded?uiI18n.t('Gespeichert')+' ('+item.format.toUpperCase()+', '+Math.ceil(item.bytes/1024)+' KiB)':uiI18n.t('Noch nicht hochgeladen')}"
        "async function loadSiteTitle(){const r=await fetch('/api/site-title',{cache:'no-store'});if(!r.ok)throw new Error(await r.text());const data=await r.json();savedSiteTitle=data.title;siteTitleHeading.textContent=data.title;if(siteTitleInput.hidden)siteTitleInput.value=data.title;document.title=data.title}"
        "async function loadBranding(){const r=await fetch('/api/branding',{cache:'no-store'});if(!r.ok)throw new Error(await r.text());brandingData=await r.json();renderBranding()}"
        "async function uploadBranding(event){event.preventDefault();const form=event.target,kind=form.dataset.branding,input=form.querySelector('input'),file=input.files[0],button=form.querySelector('button');if(!file)return;const name=file.name.toLowerCase();let mime='';if(name.endsWith('.png'))mime='image/png';else if(kind==='logo'&&name.endsWith('.svg'))mime='image/svg+xml';else if(kind==='favicon'&&name.endsWith('.ico'))mime='image/x-icon';if(!mime){showStatus('Nicht unterstuetzter Dateityp.',true);return}if(file.size===0||file.size>32748){showStatus('Datei ist zu gross (maximal 32 KiB).',true);return}button.disabled=true;input.disabled=true;try{const r=await fetch('/api/branding?kind='+kind,{method:'PUT',headers:{'Content-Type':mime},body:file});if(await showSaveResult(r)){input.value='';await loadBranding();if(kind==='logo'){const image=document.querySelector('.brand img');image.parentElement.classList.remove('has-banner');image.src='/logo.svg?v='+Date.now()}else document.querySelector('link[rel=icon]').href='/favicon.ico?v='+Date.now()}}catch(error){showStatus('Fehler: '+error.message,true)}finally{button.disabled=false;input.disabled=false}}"
        "const deviceInfo=document.querySelector('#device-info'),boardDetails=document.querySelector('#board-details');"
        "function formatBytes(value){return Number(value).toLocaleString(uiI18n.language==='de'?'de-DE':'en-US')+' B'}"
        "function formatMhz(hz){return hz?(hz/1000000).toLocaleString(uiI18n.language==='de'?'de-DE':'en-US',{maximumFractionDigits:1})+' MHz':uiI18n.t('Unbekannt')}"
        "function formatUptime(seconds){const days=Math.floor(seconds/86400),hours=Math.floor(seconds%86400/3600),minutes=Math.floor(seconds%3600/60);return(days?days+(uiI18n.language==='de'?' T ':' d '):'')+hours+' h '+minutes+' min'}"
        "async function loadDeviceInfo(){try{const r=await fetch('/api/device-info');if(!r.ok)throw new Error();const d=await r.json();const groups=["
        "['Chip',[['Modell',uiI18n.t(d.chip)],['Revision',d.chip_revision_major+'.'+d.chip_revision_minor],['CPU-Kerne',String(d.cores)],['Aktueller Takt',formatMhz(d.cpu_hz)],['Maximaler Takt',formatMhz(d.cpu_max_hz)],['Bus-Takt',formatMhz(d.apb_hz)],['Quarz',formatMhz(d.xtal_hz)]]],"
        "['Speicher',[['Flash',d.flash_bytes?formatBytes(d.flash_bytes):'Unbekannt'],['Heap intern frei',formatBytes(d.internal_free_bytes)],['Heap intern gesamt',formatBytes(d.internal_total_bytes)],['Heap Minimum',formatBytes(d.internal_min_bytes)],['Größter Block',formatBytes(d.internal_largest_bytes)],['PSRAM frei',formatBytes(d.psram_free_bytes)],['PSRAM gesamt',formatBytes(d.psram_total_bytes)]]],"
        "['Firmware',[['Version',d.firmware_version],['Build',d.build_date+' '+d.build_time],['ESP-IDF',d.idf_version],['Laufzeit',formatUptime(d.uptime_seconds)]]],"
        "['WLAN',[['Name',d.hostname?d.hostname+'.local':'Unbekannt'],['MAC-Adresse',d.wifi_mac||'Unbekannt'],['IPv4',d.wifi_ipv4||'Nicht vorhanden'],['IPv6 global',d.wifi_ipv6_global||'Nicht vorhanden'],['IPv6 lokal',d.wifi_ipv6_linklocal||'Nicht vorhanden'],['Signal',d.wifi_rssi_dbm===undefined?'Nicht verbunden':d.wifi_rssi_dbm+' dBm'],['Kanal',d.wifi_channel===undefined?'Unbekannt':String(d.wifi_channel)]]]];"
        "deviceInfo.replaceChildren(...groups.map(([title,fields])=>{const group=document.createElement('section'),heading=document.createElement('h3'),list=document.createElement('dl');group.className='device-group';heading.textContent=uiI18n.t(title);list.className='device-metrics';list.append(...fields.map(([label,value])=>{const item=document.createElement('div'),term=document.createElement('dt'),description=document.createElement('dd');item.className='device-metric';term.textContent=uiI18n.t(label);description.textContent=uiI18n.t(value);item.append(term,description);return item}));group.append(heading,list);return group}))}"
        "catch{const message=document.createElement('p');message.className='device-message';message.textContent=uiI18n.t('Gerätedaten konnten nicht geladen werden.');deviceInfo.replaceChildren(message)}}"
        "let toastTimeout;function showToast(message){toast.textContent=uiI18n.response(message);toast.classList.add('show');clearTimeout(toastTimeout);toastTimeout=setTimeout(()=>toast.classList.remove('show'),2600)}"
        "async function showSaveResult(response){const message=await response.text();if(response.ok){showStatus('');showToast(message);return true}showStatus(message,true);return false}"
        "function setJobLoading(visible){jobLoading.classList.toggle('show',visible);jobLoading.setAttribute('aria-hidden',String(!visible))}"
        "function setJenkinsLoading(visible){jenkinsLoading.classList.toggle('show',visible);jenkinsLoading.setAttribute('aria-hidden',String(!visible))}"
        "async function refreshJenkinsRequestState(){try{const r=await fetch('/api/jenkins-request');if(!r.ok)return;const data=await r.json();setJenkinsLoading(data.in_progress)}catch{}}"
        "let jobCache=null,jobCacheUsername='',jobListNoteSource='Liste noch nicht geladen.';function jobCacheKey(){return 'ci-lights:jobs:'+loginUsername.value.trim()}"
        "function setJobListNote(message){jobListNoteSource=message;jobListNote.textContent=uiI18n.t(message)}"
        "function clearJobList(message='Liste noch nicht geladen.'){jobCache=null;jobCacheUsername='';select.replaceChildren();select.disabled=true;showJob.disabled=true;setJobListNote(message)}"
        "function renderJobs(data,message){if(!data||!Array.isArray(data.jobs)||!data.jobs.every(job=>job&&typeof job.name==='string'&&typeof job.url==='string'))return false;jobCache=data;jobCacheUsername=loginUsername.value.trim();select.replaceChildren(...data.jobs.map(job=>{const option=new Option(job.name,job.url);option.selected=job.url===data.selected_url;return option}));select.disabled=!data.jobs.length;showJob.disabled=!data.jobs.length;setJobListNote(data.jobs.length?message:'Keine Jobs gefunden.');return true}"
        "function cacheJobs(){try{localStorage.setItem(jobCacheKey(),JSON.stringify(jobCache))}catch{}}"
        "function restoreJobs(){if(jobCache&&jobCacheUsername===loginUsername.value.trim()&&renderJobs(jobCache,'Gespeicherte Jobliste.'))return;try{const saved=localStorage.getItem(jobCacheKey());if(saved&&renderJobs(JSON.parse(saved),'Gespeicherte Jobliste.'))return}catch{}clearJobList()}"
        "function invalidateJobs(){try{localStorage.removeItem(jobCacheKey())}catch{}clearJobList('Jenkins-Zugang geändert. Jobliste aktualisieren.')}"
        "async function loadJobs(){refreshJobs.disabled=true;setJobLoading(true);showStatus('');try{const r=await fetch('/api/jobs',{cache:'no-store'});if(!r.ok)throw new Error(await r.text());const data=await r.json();if(!renderJobs(data,'Jobliste aktualisiert.'))throw new Error('Ungültige Jobliste.');cacheJobs()}finally{refreshJobs.disabled=false;setJobLoading(false)}}"
        "refreshJobs.addEventListener('click',()=>loadJobs().catch(error=>showStatus('Fehler: '+error.message,true)));"
        "async function loadPollInterval(){const r=await fetch('/api/poll-interval');if(!r.ok)throw new Error(await r.text());"
        "const data=await r.json();pollInterval.value=data.minutes}"
        "function showTab(tab){document.querySelectorAll('.tab').forEach(section=>section.classList.toggle('active',section.id==='tab-'+tab));"
        "document.querySelectorAll('[data-tab]').forEach(tabLink=>tabLink.setAttribute('aria-selected',tabLink.dataset.tab===tab));"
        "if(tab==='manual')loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true));if(tab==='jenkins')loadBranding().catch(error=>showStatus('Fehler: '+error.message,true));if(tab==='jenkins'&&!wifiTabLoaded&&!wifiTabLoading){wifiTabLoading=true;loadSavedWifi().then(()=>wifiTabLoaded=true).catch(error=>showStatus('Fehler: '+error.message,true)).finally(()=>wifiTabLoading=false);loadWifiNetworks().catch(error=>showWifiScanStatus('Fehler: '+error.message))}}"
        "document.querySelectorAll('[data-tab]').forEach(tabLink=>tabLink.addEventListener('click',event=>{event.preventDefault();showTab(tabLink.dataset.tab)}));"
        "const lightLabels={red:'Rot',yellow:'Gelb',green:'Grün'},modeHints={auto:'Jenkins steuert die Ampel.',manual:'Die LEDs können einzeln geschaltet werden.',disco:'Die WS2812-Ampel spielt die gewählte Disco-Animation.',api:'Der Status wird ausschließlich per REST API gesetzt.'};"
        "const knownLightColors=Object.fromEntries([...document.querySelectorAll('[data-light-color]')].map(input=>[input.dataset.lightColor,input.value]));let lightColorRevision=0;"
        "function setControlMode(mode){controlMode.value=mode;modeHint.textContent=uiI18n.t(modeHints[mode]||'');document.querySelectorAll('[data-light]').forEach(button=>button.disabled=mode!=='manual');document.querySelectorAll('[data-light-color]').forEach(input=>input.disabled=mode!=='manual');const discoActive=mode==='disco';discoControls.hidden=!discoActive;discoEffect.disabled=!discoActive}"
        "function setBrightness(value){brightness.value=value;brightnessValue.value=value;brightnessValue.textContent=value+' %'}"
        "function setLightButton(light,enabled){const button=document.querySelector('[data-light=\"'+light+'\"]');"
        "button.dataset.enabled=enabled;button.setAttribute('aria-pressed',enabled);"
        "button.textContent=uiI18n.t(lightLabels[light])+': '+uiI18n.t(enabled?'An':'Aus');const svg=document.querySelector('#traffic-light').contentDocument;"
        "if(svg){const lamp=svg.getElementById(light);if(lamp)lamp.classList.toggle('is-on',enabled)}"
        "const states=[...document.querySelectorAll('[data-light]')].map(item=>uiI18n.t(lightLabels[item.dataset.light])+': '+uiI18n.t(item.dataset.enabled==='true'?'An':'Aus'));document.querySelector('#light-state').textContent=states.join(' | ')}"
        "let currentGrey=false;function setGreyDisplay(grey){currentGrey=grey;const svg=document.querySelector('#traffic-light').contentDocument;if(svg)svg.documentElement.classList.toggle('jenkins-grey',grey);if(grey)document.querySelector('#light-state').textContent=uiI18n.t('Jenkins: Grau')}"
        "async function loadLightStates(forceColor){const revision=lightColorRevision;const r=await fetch('/api/lights');if(!r.ok)throw new Error(await r.text());"
        "const data=await r.json();for(const light of Object.keys(lightLabels)){setLightButton(light,data[light]);const color=document.querySelector('[data-light-color=\"'+light+'\"]');if(color&&revision===lightColorRevision){const serverColor=data[light+'_color'];if((light===forceColor||color.value===knownLightColors[light])&&color.value!==serverColor)color.value=serverColor;knownLightColors[light]=serverColor}}setGreyDisplay(data.mode==='auto'&&data.grey===true);const svg=document.querySelector('#traffic-light').contentDocument;if(svg)for(const light of Object.keys(lightLabels)){const lamp=svg.getElementById(light);if(lamp)lamp.classList.toggle('is-pulsing',data.mode==='auto'&&data.pulsing===true&&data[light]===true)}setControlMode(data.mode);discoEffect.value=data.disco_effect;setBrightness(data.brightness)}"
        "document.querySelector('#jenkins-form').addEventListener('submit',async event=>{event.preventDefault();"
        "const r=await fetch('/api/jenkins',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(Object.fromEntries(new FormData(event.target)))});"
        "if(await showSaveResult(r)){invalidateJobs();showTab('job')}});"
        "document.querySelector('#poll-interval-form').addEventListener('submit',async event=>{event.preventDefault();"
        "const minutes=Number(pollInterval.value);if(!Number.isInteger(minutes)||minutes<1){showStatus('Bitte mindestens 1 ganze Minute angeben.',true);return}"
        "const r=await fetch('/api/poll-interval',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({poll_interval_minutes:minutes})});await showSaveResult(r)});"
        "brandingForms.forEach(form=>form.addEventListener('submit',uploadBranding));const bannerForm=document.querySelector('#banner-form'),bannerFile=document.querySelector('#banner-file');bannerForm.querySelector('.banner-upload').addEventListener('click',()=>bannerFile.click());bannerFile.addEventListener('change',()=>{if(bannerFile.files.length)bannerForm.requestSubmit()});"
        "function closeSiteTitleEditor(){siteTitleInput.hidden=true;siteTitleButton.hidden=false;siteTitleButton.setAttribute('aria-expanded','false')}siteTitleButton.addEventListener('click',()=>{if(siteTitleButton.disabled)return;siteTitleInput.value=siteTitleHeading.textContent;siteTitleButton.hidden=true;siteTitleInput.hidden=false;siteTitleButton.setAttribute('aria-expanded','true');siteTitleInput.focus();siteTitleInput.select()});siteTitleInput.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();siteTitleInput.blur()}else if(event.key==='Escape'){siteTitleInput.value=savedSiteTitle;siteTitleInput.blur()}});siteTitleInput.addEventListener('blur',async()=>{const title=siteTitleInput.value.trim();closeSiteTitleEditor();if(title===savedSiteTitle)return;if(!title){siteTitleInput.value=savedSiteTitle;showStatus('Ungueltiger Titel.',true);return}siteTitleHeading.textContent=title;document.title=title;siteTitleButton.disabled=true;try{const r=await fetch('/api/site-title',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({title})});if(!r.ok)throw new Error(await r.text());const data=await r.json();savedSiteTitle=data.title;siteTitleHeading.textContent=data.title;siteTitleInput.value=data.title;document.title=data.title;showStatus('');showToast('Titel gespeichert.')}catch(error){siteTitleHeading.textContent=savedSiteTitle;siteTitleInput.value=savedSiteTitle;document.title=savedSiteTitle;showStatus('Fehler: '+error.message,true)}finally{siteTitleButton.disabled=false}});"
        "wifiRefresh.addEventListener('click',()=>loadWifiNetworks().catch(error=>showWifiScanStatus('Fehler: '+error.message)));"
        "wifiForm.addEventListener('submit',async event=>{event.preventDefault();const button=wifiForm.querySelector('[type=\"submit\"]');button.disabled=true;try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Object.fromEntries(new FormData(wifiForm)))});showStatus(await r.text(),!r.ok);if(!r.ok)button.disabled=false}catch(error){showStatus('Fehler: '+error.message,true);button.disabled=false}});"
        "controlMode.addEventListener('change',async()=>{const mode=controlMode.value;if(mode==='auto')setJenkinsLoading(true);try{const r=await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode})});if(await showSaveResult(r)){setControlMode(mode);loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))}else loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))}finally{if(mode==='auto')setJenkinsLoading(false)}});"
        "discoEffect.addEventListener('change',async()=>{const r=await fetch('/api/disco-effect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({effect:discoEffect.value})});if(!await showSaveResult(r))loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))});"
        "brightness.addEventListener('input',()=>setBrightness(brightness.value));brightness.addEventListener('change',async()=>{const r=await fetch('/api/brightness',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:Number(brightness.value)})});if(!await showSaveResult(r))loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))});"
        "document.querySelectorAll('[data-light]').forEach(button=>button.addEventListener('click',async()=>{"
        "const enabled=button.dataset.enabled!=='true';"
        "const r=await fetch('/api/light',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({light:button.dataset.light,enabled})});"
        "if(await showSaveResult(r))setLightButton(button.dataset.light,enabled)}));"
        "document.querySelectorAll('[data-light-color]').forEach(input=>{input.addEventListener('input',()=>lightColorRevision++);input.addEventListener('change',async()=>{const light=input.dataset.lightColor,chosenColor=input.value;let saved=false;lightColorRevision++;try{const r=await fetch('/api/light-color',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({light,color:chosenColor})});saved=await showSaveResult(r);if(saved)knownLightColors[light]=chosenColor}catch(error){showStatus('Fehler: '+error.message,true)}finally{lightColorRevision++;if(!saved)loadLightStates(light).catch(error=>showStatus('Fehler: '+error.message,true))}})});"
        "document.querySelector('#job-form').addEventListener('submit',async event=>{event.preventDefault();setJenkinsLoading(true);try{"
        "const r=await fetch('/api/job',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({url:select.value})});if(await showSaveResult(r)){if(jobCache){jobCache.selected_url=select.value;cacheJobs()}loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))}}finally{setJenkinsLoading(false)}});"
        "document.querySelector('#traffic-light').addEventListener('load',()=>{if(adminAuthorization)loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true))});"
        "uiI18n.onChange(()=>{document.title=siteTitleHeading.textContent;showStatus(statusSource,status.dataset.error==='true');showWifiScanStatus(wifiScanSource);setJobListNote(jobListNoteSource);renderSavedWifi();renderBranding();setControlMode(controlMode.value);for(const light of Object.keys(lightLabels)){const button=document.querySelector('[data-light=\"'+light+'\"]');setLightButton(light,button.dataset.enabled==='true')}setGreyDisplay(currentGrey);if(boardDetails.open)loadDeviceInfo()});"
        "function refreshDashboard(){restoreJobs();loadSiteTitle().catch(error=>showStatus('Fehler: '+error.message,true));loadLightStates().catch(error=>showStatus('Fehler: '+error.message,true));loadPollInterval().catch(error=>showStatus('Fehler: '+error.message,true));refreshJenkinsRequestState();if(boardDetails.open)loadDeviceInfo()}"
        "function startDashboard(){dashboardStarted=true;uiI18n.start();refreshDashboard();setInterval(()=>{if(!document.hidden&&adminAuthorization)loadLightStates().catch(()=>{})},1000);document.addEventListener('visibilitychange',()=>{if(!document.hidden&&adminAuthorization)loadLightStates().catch(()=>{})});boardDetails.addEventListener('toggle',()=>{if(boardDetails.open&&adminAuthorization)loadDeviceInfo()});setInterval(()=>{if(boardDetails.open&&adminAuthorization)loadDeviceInfo()},60000);setInterval(()=>{if(adminAuthorization)refreshJenkinsRequestState()},3000)}</script></body></html>";

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(HTTP_TAG, "GET / nach %lld ms beendet: %s",
             (long long) ((esp_timer_get_time() - request_start_us) / 1000),
             esp_err_to_name(err));
    return err;
}

static esp_err_t status_login_handler(httpd_req_t *request)
{
    char body[256];
    if (request->content_len <= 0 || request->content_len >= sizeof(body)) {
        return send_status_error(request, "400 Bad Request", "Ungültige Anmeldedaten.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            mbedtls_platform_zeroize(body, sizeof(body));
            return send_status_error(request, "400 Bad Request",
                                     "Anmeldedaten konnten nicht gelesen werden.");
        }
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(json, "username");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    bool valid = cJSON_IsString(username) && cJSON_IsString(password) &&
                 username->valuestring != NULL && password->valuestring != NULL &&
                 app_admin_credentials_verify(username->valuestring, password->valuestring);
    cJSON_Delete(json);
    mbedtls_platform_zeroize(body, sizeof(body));
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (!valid) {
        return send_status_error(request, "401 Unauthorized",
                                 "Benutzername oder Passwort falsch.");
    }
    return httpd_resp_sendstr(request, "OK");
}

static const httpd_uri_t status_page_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = status_page_handler,
};

static const httpd_uri_t status_login_uri = {
    .uri = "/api/login",
    .method = HTTP_POST,
    .handler = status_login_handler,
};

static const httpd_uri_t status_device_info_uri = {
    .uri = "/api/device-info",
    .method = HTTP_GET,
    .handler = status_device_info_handler,
};

static const httpd_uri_t status_wifi_get_uri = {
    .uri = "/api/wifi",
    .method = HTTP_GET,
    .handler = status_wifi_get_handler,
};

static const httpd_uri_t status_wifi_post_uri = {
    .uri = "/api/wifi",
    .method = HTTP_POST,
    .handler = provision_config_handler,
};

static const httpd_uri_t status_wifi_delete_uri = {
    .uri = "/api/wifi",
    .method = HTTP_DELETE,
    .handler = status_wifi_delete_handler,
};

static const httpd_uri_t status_jobs_uri = {
    .uri = "/api/jobs",
    .method = HTTP_GET,
    .handler = status_jobs_handler,
};

static const httpd_uri_t status_jenkins_uri = {
    .uri = "/api/jenkins",
    .method = HTTP_POST,
    .handler = status_jenkins_handler,
};

static const httpd_uri_t status_jenkins_request_state_uri = {
    .uri = "/api/jenkins-request",
    .method = HTTP_GET,
    .handler = status_jenkins_request_state_handler,
};

static const httpd_uri_t status_poll_interval_get_uri = {
    .uri = "/api/poll-interval",
    .method = HTTP_GET,
    .handler = status_poll_interval_get_handler,
};

static const httpd_uri_t status_poll_interval_uri = {
    .uri = "/api/poll-interval",
    .method = HTTP_POST,
    .handler = status_poll_interval_handler,
};

static const httpd_uri_t status_job_uri = {
    .uri = "/api/job",
    .method = HTTP_POST,
    .handler = status_job_handler,
};

static const httpd_uri_t status_manual_light_uri = {
    .uri = "/api/light",
    .method = HTTP_POST,
    .handler = status_manual_light_handler,
};

static const httpd_uri_t status_manual_light_color_uri = {
    .uri = "/api/light-color",
    .method = HTTP_POST,
    .handler = status_manual_light_color_handler,
};

static const httpd_uri_t status_manual_lights_state_uri = {
    .uri = "/api/lights",
    .method = HTTP_GET,
    .handler = status_manual_lights_state_handler,
};

static const httpd_uri_t status_disco_effect_uri = {
    .uri = "/api/disco-effect",
    .method = HTTP_POST,
    .handler = status_disco_effect_handler,
};

static const httpd_uri_t status_brightness_uri = {
    .uri = "/api/brightness",
    .method = HTTP_POST,
    .handler = status_brightness_handler,
};

static const httpd_uri_t status_control_mode_uri = {
    .uri = "/api/mode",
    .method = HTTP_POST,
    .handler = status_control_mode_handler,
};

static const httpd_uri_t status_api_status_uri = {
    .uri = "/api/status",
    .method = HTTP_POST,
    .handler = status_api_status_handler,
};

void app_start_status_server(const app_config_t *config,
                             app_job_selected_handler_t job_selected_handler,
                             app_manual_light_handler_t manual_light_handler,
                             app_manual_light_state_handler_t manual_light_state_handler,
                             app_control_mode_handler_t control_mode_handler,
                             app_control_mode_state_handler_t control_mode_state_handler,
                             app_api_status_handler_t api_status_handler,
                             app_disco_effect_handler_t disco_effect_handler,
                             app_disco_effect_state_handler_t disco_effect_state_handler,
                             app_light_brightness_handler_t light_brightness_handler,
                             app_light_brightness_state_handler_t light_brightness_state_handler)
{
    if (config == NULL || s_status_server != NULL) {
        return;
    }

    s_status_config = *config;
    s_job_selected_handler = job_selected_handler;
    s_manual_light_handler = manual_light_handler;
    s_manual_light_state_handler = manual_light_state_handler;
    s_control_mode_handler = control_mode_handler;
    s_control_mode_state_handler = control_mode_state_handler;
    s_api_status_handler = api_status_handler;
    s_disco_effect_handler = disco_effect_handler;
    s_disco_effect_state_handler = disco_effect_state_handler;
    s_light_brightness_handler = light_brightness_handler;
    s_light_brightness_state_handler = light_brightness_state_handler;

    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAC-Adresse konnte nicht gelesen werden: %s", esp_err_to_name(err));
        return;
    }
    snprintf(s_light_hostname, sizeof(s_light_hostname), "ci-lights-%02x%02x",
             mac[4], mac[5]);

    err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS konnte nicht gestartet werden: %s", esp_err_to_name(err));
        return;
    }
    if ((err = mdns_hostname_set(s_light_hostname)) != ESP_OK ||
        (err = mdns_instance_name_set("CI-Lights")) != ESP_OK) {
        ESP_LOGE(TAG, "mDNS-Name konnte nicht gesetzt werden: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 30;
    server_config.stack_size = STATUS_HTTP_TASK_STACK_SIZE;
    server_config.lru_purge_enable = true;
    err = httpd_start(&s_status_server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Status-Webserver konnte nicht gestartet werden: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }
    ESP_LOGI(HTTP_TAG, "Status-Webserver gestartet; interner Heap frei %lu B",
             (unsigned long) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if ((err = httpd_register_uri_handler(s_status_server, &status_page_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_status_server, &status_login_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_device_info_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_wifi_get_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_wifi_post_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_wifi_delete_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &provision_wifi_networks_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_status_server, &logo_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_status_server, &favicon_ico_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &branding_status_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &branding_upload_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_status_server, &traffic_light_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_status_server, &localization_js_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &language_get_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &language_post_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &site_title_get_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &site_title_post_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_jobs_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_jenkins_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_jenkins_request_state_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_poll_interval_get_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_poll_interval_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_job_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_manual_light_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_manual_light_color_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_manual_lights_state_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_disco_effect_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_brightness_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_control_mode_uri)) != ESP_OK ||
        (err = register_protected_uri(s_status_server, &status_api_status_uri)) != ESP_OK) {
        ESP_LOGE(TAG, "Status-Webserver konnte nicht eingerichtet werden: %s", esp_err_to_name(err));
        httpd_stop(s_status_server);
        s_status_server = NULL;
        mdns_free();
        return;
    }

    err = esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID,
                                     status_http_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(HTTP_TAG, "HTTP-Diagnoseereignisse konnten nicht aktiviert werden: %s",
                 esp_err_to_name(err));
    }

    err = mdns_service_add("CI-Lights", "_http", "_tcp", server_config.server_port,
                           NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP-mDNS-Dienst konnte nicht angekuendigt werden: %s",
                 esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Ampel-Webseite: http://%s.local", s_light_hostname);
}

static void generate_provision_password(
    uint8_t symbols[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH],
    char password[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH * 2 + 1])
{
    static const char position_codes[] = "TMB";
    static const char color_codes[] = "ROYGBV";
    /* 252 is the largest multiple of 18 below 256: rejection keeps each
     * position/color combination equally likely. */
    bootloader_random_enable();
    for (size_t i = 0; i < TRAFFIC_LIGHT_PROVISION_CODE_LENGTH; ++i) {
        uint8_t random_byte;
        do {
            esp_fill_random(&random_byte, sizeof(random_byte));
        } while (random_byte >= 252);
        symbols[i] = random_byte % TRAFFIC_LIGHT_PROVISION_SYMBOL_COUNT;
        password[i * 2] = position_codes[symbols[i] / TRAFFIC_LIGHT_PROVISION_COLOR_COUNT];
        password[i * 2 + 1] = color_codes[symbols[i] % TRAFFIC_LIGHT_PROVISION_COLOR_COUNT];
    }
    bootloader_random_disable();
    password[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH * 2] = '\0';
}

void app_start_provisioning(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        snprintf(s_provision_ssid, sizeof(s_provision_ssid),
                 "ci-lights-%02x%02x-setup", mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "MAC-Adresse fuer Einrichtungs-WLAN nicht lesbar: %s",
                 esp_err_to_name(err));
        snprintf(s_provision_ssid, sizeof(s_provision_ssid), "ci-lights-setup");
    }

    uint8_t symbols[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH];
    char password[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH * 2 + 1];
    generate_provision_password(symbols, password);

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(ap_netif == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, s_provision_ssid, strlen(s_provision_ssid));
    memcpy(ap_config.ap.password, password, sizeof(password));
    ap_config.ap.ssid_len = strlen(s_provision_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA3_PSK;
    ap_config.ap.pmf_cfg.required = true;
    ap_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    mbedtls_platform_zeroize(password, sizeof(password));
    mbedtls_platform_zeroize(ap_config.ap.password, sizeof(ap_config.ap.password));
    ESP_ERROR_CHECK(esp_wifi_start());
    traffic_light_start_provision_code_animation(symbols);
    mbedtls_platform_zeroize(symbols, sizeof(symbols));
    configure_captive_portal_dhcp(ap_netif);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 10;
    server_config.stack_size = 10240;
    ESP_ERROR_CHECK(httpd_start(&s_provision_server, &server_config));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &provision_page_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &logo_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &favicon_ico_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &traffic_light_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &localization_js_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &language_get_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &language_post_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &provision_config_uri));
    ESP_ERROR_CHECK(register_protected_uri(s_provision_server, &provision_wifi_networks_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_provision_server, &provision_ios_probe_uri));
    ESP_ERROR_CHECK(httpd_register_err_handler(s_provision_server, HTTPD_404_NOT_FOUND,
                                               provision_not_found_handler));
    if (xTaskCreate(captive_dns_task, "captive_dns", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Captive-Portal-DNS-Task konnte nicht gestartet werden");
    } else {
        ESP_LOGI(TAG, "Captive-Portal-DNS ist aktiv");
    }

    ESP_LOGI(TAG, "Einrichtungs-WLAN '%s' verwendet WPA3-SAE", s_provision_ssid);
    ESP_LOGI(TAG, "LED-Code: 8 Signale, je Position (T/M/B) und Farbe (R/O/Y/G/B/V)");
    ESP_LOGI(TAG, "Captive Portal: %s", s_provision_url);
}
