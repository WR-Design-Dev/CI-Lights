#include "app_ota.h"
#include "app_security.h"
#include "app_ota_policy.h"
#include "app_ota_storage.h"
#include "app_timezones.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "sdkconfig.h"

#if !CONFIG_SECURE_SIGNED_ON_UPDATE || !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "CI-Lights OTA requires signed updates and boot rollback"
#endif

#define OTA_SLOT_SIZE 0x1C0000U
#define OTA_MANIFEST_LIMIT 4096U
#define OTA_HTTP_URL_LIMIT 4096U
#define OTA_URL_LIMIT 512U
#define OTA_VERSION_LIMIT 32U
#define OTA_REPOSITORY CONFIG_APP_OTA_GITHUB_REPOSITORY
#if CONFIG_APP_SECURITY_SECURE
#define OTA_MANIFEST_URL "https://github.com/" OTA_REPOSITORY "/releases/latest/download/ota-manifest-secure.json"
#define OTA_IMAGE_NAME "CI-Lights-Secure.bin"
#else
#define OTA_MANIFEST_URL "https://github.com/" OTA_REPOSITORY "/releases/latest/download/ota-manifest.json"
#define OTA_IMAGE_NAME "CI-Lights.bin"
#endif

typedef struct {
    char version[OTA_VERSION_LIMIT];
    char url[OTA_URL_LIMIT];
    uint32_t size;
    uint8_t sha256[32];
} ota_manifest_t;

typedef struct {
    const char *phase;
    const char *message;
    char latest_version[OTA_VERSION_LIMIT];
    uint32_t bytes;
    uint32_t total;
    bool busy;
    bool boot_pending;
    bool configured;
} ota_status_t;

typedef struct {
    esp_http_client_handle_t client;
    char location[OTA_HTTP_URL_LIMIT];
    bool bad_header;
} ota_http_stream_t;

static const char *TAG = "ota";
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t s_status = {.phase = "idle", .message = "Noch nicht nach Updates gesucht."};
static char s_requested_version[OTA_VERSION_LIMIT];
static bool s_boot_validation_started;
static app_ota_settings_t s_settings;
static app_ota_history_t s_history;
static bool s_storage_ok;
static bool s_clock_synced;
static bool s_automation_started;
static int64_t s_next_check_us;
static int64_t s_automation_start_us;
static unsigned s_failures;
static uint32_t s_settings_revision;

typedef enum { OTA_MANUAL_CHECK, OTA_MANUAL_INSTALL, OTA_AUTO_CHECK, OTA_AUTO_INSTALL } ota_request_t;
static void set_phase(const char *phase, const char *message);

bool app_ota_begin_config_transfer(void)
{
    portENTER_CRITICAL(&s_status_lock);
    bool allowed = !s_status.busy && !s_status.boot_pending;
    if (allowed) s_status.busy = true;
    portEXIT_CRITICAL(&s_status_lock);
    return allowed;
}

void app_ota_end_config_transfer(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.busy = false;
    portEXIT_CRITICAL(&s_status_lock);
}

static bool clock_valid(void)
{
    portENTER_CRITICAL(&s_status_lock);
    bool synced = s_clock_synced;
    portEXIT_CRITICAL(&s_status_lock);
    return synced && app_timezone_time_supported((int64_t) time(NULL));
}

static bool persist_history(void)
{
    app_ota_history_t history;
    portENTER_CRITICAL(&s_status_lock);
    history = s_history;
    portEXIT_CRITICAL(&s_status_lock);
    esp_err_t err = app_ota_storage_save_history(&history);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update-Verlauf konnte nicht gespeichert werden: %s", esp_err_to_name(err));
        portENTER_CRITICAL(&s_status_lock);
        s_storage_ok = false;
        portEXIT_CRITICAL(&s_status_lock);
    }
    return err == ESP_OK;
}

static void finish_operation(app_ota_result_t result, bool failed)
{
    int64_t checked_at = clock_valid() ? (int64_t) time(NULL) : 0;
    int64_t monotonic = esp_timer_get_time();
    portENTER_CRITICAL(&s_status_lock);
    s_history.result = result;
    s_history.checked_at = checked_at;
    if (failed) { if (s_failures < 3) ++s_failures; }
    else s_failures = 0;
    uint32_t delay = failed ? app_ota_retry_seconds(s_failures) : s_settings.interval_hours * 3600;
    s_next_check_us = monotonic + (int64_t) delay * 1000000;
    portEXIT_CRITICAL(&s_status_lock);
    persist_history();
    portENTER_CRITICAL(&s_status_lock);
    s_status.busy = false;
    portEXIT_CRITICAL(&s_status_lock);
}

static void finalize_startup_history(void)
{
    const char *current = esp_app_get_description()->version;
    app_ota_history_t history;
    portENTER_CRITICAL(&s_status_lock);
    history = s_history;
    portEXIT_CRITICAL(&s_status_lock);
    if (!app_ota_history_finish_boot(&history, current, clock_valid() ? (int64_t) time(NULL) : 0)) return;
    if (history.result == APP_OTA_RESULT_ROLLED_BACK) {
        set_phase("rollback", "Die neue Firmware wurde zurückgerollt. Diese Version wird automatisch übersprungen.");
    }
    portENTER_CRITICAL(&s_status_lock);
    s_history = history;
    portEXIT_CRITICAL(&s_status_lock);
    persist_history();
}

static void set_phase(const char *phase, const char *message)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.phase = phase;
    s_status.message = message;
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_error(esp_err_t err, const char *message)
{
    ESP_LOGW(TAG, "%s (%s)", message, esp_err_to_name(err));
    set_phase("error", message);
}

static const char *json_string(const cJSON *json, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static esp_err_t parse_manifest(const char *body, ota_manifest_t *manifest)
{
    cJSON *json = cJSON_ParseWithOpts(body, NULL, true);
    const char *version = json_string(json, "version");
    const char *project = json_string(json, "project");
    const char *chip = json_string(json, "chip");
    const char *layout = json_string(json, "layout");
    const char *profile = json_string(json, "profile");
    const char *url = json_string(json, "url");
    const char *sha256 = json_string(json, "sha256");
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(json, "size");
    uint32_t version_parts[3];
    char expected_url[OTA_URL_LIMIT];
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (!cJSON_IsObject(json) || !cJSON_IsNumber(schema) || schema->valuedouble != 1 ||
        !app_ota_parse_version(version, version_parts) || project == NULL ||
        strcmp(project, app_security_project()) != 0 || chip == NULL || strcmp(chip, "esp32s3") != 0 ||
        (profile != NULL && strcmp(profile, app_security_profile()) != 0) ||
        (CONFIG_APP_SECURITY_SECURE && profile == NULL) ||
        layout == NULL || strcmp(layout, app_security_layout()) != 0 || url == NULL ||
        strlen(url) >= sizeof(manifest->url) || sha256 == NULL || strlen(sha256) != 64 ||
        !cJSON_IsNumber(size) || !(size->valuedouble >= 4096 && size->valuedouble <= OTA_SLOT_SIZE) ||
        size->valuedouble != (uint32_t) size->valuedouble) {
        goto done;
    }
    int length = snprintf(expected_url, sizeof(expected_url),
                          "https://github.com/" OTA_REPOSITORY "/releases/download/v%s/" OTA_IMAGE_NAME,
                          version);
    if (length < 0 || length >= sizeof(expected_url) || strcmp(url, expected_url) != 0) {
        goto done;
    }
    for (unsigned i = 0; i < sizeof(manifest->sha256); ++i) {
        int high = hex_digit(sha256[i * 2]), low = hex_digit(sha256[i * 2 + 1]);
        if (high < 0 || low < 0) goto done;
        manifest->sha256[i] = (uint8_t) ((high << 4) | low);
    }
    strcpy(manifest->version, version);
    strcpy(manifest->url, url);
    manifest->size = (uint32_t) size->valuedouble;
    err = ESP_OK;
done:
    cJSON_Delete(json);
    return err;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        ota_http_stream_t *stream = event->user_data;
        if (strcasecmp(event->header_key, "Location") == 0) {
            size_t length = strlen(event->header_value);
            if (length >= sizeof(stream->location)) {
                stream->bad_header = true;
            } else {
                memcpy(stream->location, event->header_value, length + 1);
            }
        } else if (strcasecmp(event->header_key, "Content-Encoding") == 0 &&
                   strcasecmp(event->header_value, "identity") != 0) {
            stream->bad_header = true;
        }
    }
    return ESP_OK;
}

static void close_stream(ota_http_stream_t *stream)
{
    if (stream->client != NULL) {
        esp_http_client_close(stream->client);
        esp_http_client_cleanup(stream->client);
        stream->client = NULL;
    }
}

/* Handle redirects explicitly so that every hop must remain HTTPS. */
static esp_err_t open_stream(const char *url, ota_http_stream_t *stream, int64_t *length)
{
    char *next = malloc(OTA_HTTP_URL_LIMIT);
    if (next == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (strlen(url) >= OTA_HTTP_URL_LIMIT) goto done;
    strcpy(next, url);
    for (unsigned redirect = 0; redirect < 6; ++redirect) {
        if (strncmp(next, "https://", 8) != 0) {
            err = ESP_ERR_INVALID_ARG;
            break;
        }
        stream->location[0] = '\0';
        stream->bad_header = false;
        esp_http_client_config_t config = {
            .url = next,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
            .buffer_size = 4096,
            .buffer_size_tx = 4096,
            .disable_auto_redirect = true,
            .event_handler = http_event,
            .user_data = stream,
            .user_agent = "CI-Lights-OTA/1",
        };
        stream->client = esp_http_client_init(&config);
        if (stream->client == NULL) { err = ESP_ERR_NO_MEM; break; }
        esp_http_client_set_header(stream->client, "Accept-Encoding", "identity");
        err = esp_http_client_open(stream->client, 0);
        if (err != ESP_OK) break;
        *length = esp_http_client_fetch_headers(stream->client);
        int status = esp_http_client_get_status_code(stream->client);
        if (stream->bad_header || (*length < 0 && !esp_http_client_is_chunked_response(stream->client))) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (status == 200) { err = ESP_OK; break; }
        if (status == 404) { err = ESP_ERR_NOT_FOUND; break; }
        if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
            stream->location[0] == '\0') {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (stream->location[0] == '/' && stream->location[1] != '/') {
            char *path = strchr(next + 8, '/');
            size_t origin = path == NULL ? strlen(next) : (size_t) (path - next);
            size_t location_length = strlen(stream->location);
            if (origin + location_length >= OTA_HTTP_URL_LIMIT) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            memcpy(next + origin, stream->location, location_length + 1);
        } else {
            strcpy(next, stream->location);
        }
        close_stream(stream);
        err = ESP_ERR_INVALID_RESPONSE;
    }
done:
    free(next);
    if (err != ESP_OK) close_stream(stream);
    return err;
}

static esp_err_t fetch_manifest(ota_manifest_t *manifest)
{
    char *body = calloc(1, OTA_MANIFEST_LIMIT + 1);
    ota_http_stream_t *stream = calloc(1, sizeof(*stream));
    if (body == NULL || stream == NULL) { free(body); free(stream); return ESP_ERR_NO_MEM; }
    int64_t content_length = 0;
    esp_err_t err = open_stream(OTA_MANIFEST_URL, stream, &content_length);
    if (err != ESP_OK) goto done;
    if (content_length > OTA_MANIFEST_LIMIT) { err = ESP_ERR_INVALID_SIZE; goto done; }
    size_t received = 0;
    while (received < OTA_MANIFEST_LIMIT) {
        int count = esp_http_client_read(stream->client, body + received, OTA_MANIFEST_LIMIT - received);
        if (count < 0) { err = ESP_FAIL; goto done; }
        if (count == 0) break;
        received += (size_t) count;
    }
    if (!esp_http_client_is_complete_data_received(stream->client) || received == 0 ||
        memchr(body, '\0', received) != NULL) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    body[received] = '\0';
    err = parse_manifest(body, manifest);
done:
    close_stream(stream);
    free(stream);
    free(body);
    return err;
}

static esp_err_t download_image(const ota_manifest_t *manifest)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->size > partition->size) return ESP_ERR_INVALID_SIZE;
    ota_http_stream_t *stream = calloc(1, sizeof(*stream));
    uint8_t *buffer = heap_caps_malloc_prefer(4096, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stream == NULL || buffer == NULL) { free(stream); free(buffer); return ESP_ERR_NO_MEM; }
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    mbedtls_md_context_t hash;
    mbedtls_md_init(&hash);
    int64_t content_length = 0;
    esp_err_t err = open_stream(manifest->url, stream, &content_length);
    if (err != ESP_OK) goto done;
    if (content_length > 0 && content_length != manifest->size) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    if (mbedtls_md_setup(&hash, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
        mbedtls_md_starts(&hash) != 0) { err = ESP_FAIL; goto done; }
    uint8_t first[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
    size_t first_received = 0;
    while (first_received < sizeof(first)) {
        int count = esp_http_client_read(stream->client, (char *) first + first_received,
                                         sizeof(first) - first_received);
        if (count <= 0) { err = ESP_ERR_INVALID_RESPONSE; goto done; }
        first_received += (size_t) count;
    }
    esp_image_header_t header;
    memcpy(&header, first, sizeof(header));
    esp_app_desc_t description;
    memcpy(&description, first + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
           sizeof(description));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
        description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        memchr(description.version, '\0', sizeof(description.version)) == NULL ||
        memchr(description.project_name, '\0', sizeof(description.project_name)) == NULL ||
        strcmp(description.version, manifest->version) != 0 ||
        strcmp(description.project_name, app_security_project()) != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    err = esp_ota_begin(partition, manifest->size, &ota);
    if (err != ESP_OK) goto done;
    ota_open = true;
    err = esp_ota_write(ota, first, first_received);
    if (err != ESP_OK || mbedtls_md_update(&hash, first, first_received) != 0) {
        if (err == ESP_OK) err = ESP_FAIL;
        goto done;
    }
    uint32_t received = first_received;
    while (true) {
        int count = esp_http_client_read(stream->client, (char *) buffer, 4096);
        if (count < 0) { err = ESP_FAIL; goto done; }
        if (count == 0) break;
        if ((uint32_t) count > manifest->size - received) { err = ESP_ERR_INVALID_SIZE; goto done; }
        err = esp_ota_write(ota, buffer, count);
        if (err != ESP_OK || mbedtls_md_update(&hash, buffer, count) != 0) {
            if (err == ESP_OK) err = ESP_FAIL;
            goto done;
        }
        received += (uint32_t) count;
        portENTER_CRITICAL(&s_status_lock);
        s_status.bytes = received;
        portEXIT_CRITICAL(&s_status_lock);
        taskYIELD();
    }
    if (received != manifest->size || !esp_http_client_is_complete_data_received(stream->client)) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    uint8_t digest[32];
    if (mbedtls_md_finish(&hash, digest) != 0 || memcmp(digest, manifest->sha256, sizeof(digest)) != 0) {
        err = ESP_ERR_OTA_VALIDATE_FAILED;
        goto done;
    }
    close_stream(stream);
    set_phase("verifying", "Firmware-Signatur wird geprüft.");
    /* esp_ota_end verifies the image and its RSA signature before changing boot selection. */
    err = esp_ota_end(ota);
    ota_open = false;
done:
    if (ota_open) esp_ota_abort(ota);
    close_stream(stream);
    mbedtls_md_free(&hash);
    free(stream);
    heap_caps_free(buffer);
    return err;
}

static void update_task(void *argument)
{
    ota_request_t kind = (ota_request_t) (uintptr_t) argument;
    bool install = kind == OTA_MANUAL_INSTALL || kind == OTA_AUTO_INSTALL;
    bool automatic_install = kind == OTA_AUTO_INSTALL;
    app_ota_settings_t settings;
    app_ota_history_t history;
    portENTER_CRITICAL(&s_status_lock);
    settings = s_settings;
    history = s_history;
    portEXIT_CRITICAL(&s_status_lock);
    app_ota_result_t result = APP_OTA_RESULT_CHECK_FAILED;
    bool failed = true;
    if (automatic_install && settings.schedule == APP_OTA_SCHEDULE_DAILY) {
        uint32_t day = app_ota_local_day(settings.timezone, (int64_t) time(NULL));
        portENTER_CRITICAL(&s_status_lock);
        s_history.install_day = day;
        portEXIT_CRITICAL(&s_status_lock);
        if (day == 0 || !persist_history()) {
            set_error(ESP_FAIL, "Update-Zeitplanung konnte nicht gespeichert werden.");
            goto done;
        }
    }
    ota_manifest_t manifest = {0};
    esp_err_t err = fetch_manifest(&manifest);
    if (err != ESP_OK) {
        set_error(err, err == ESP_ERR_NOT_FOUND ? "Noch kein öffentliches Firmware-Release gefunden." :
                  "Update-Prüfung fehlgeschlagen. Internet und Release-Dateien prüfen.");
        goto done;
    }
    portENTER_CRITICAL(&s_status_lock);
    strcpy(s_status.latest_version, manifest.version);
    s_status.total = manifest.size;
    portEXIT_CRITICAL(&s_status_lock);
    failed = false;
    if (!app_ota_version_newer(manifest.version, esp_app_get_description()->version)) {
        set_phase("up_to_date", "Die Firmware ist aktuell.");
        result = APP_OTA_RESULT_CURRENT;
        goto done;
    }
    result = APP_OTA_RESULT_AVAILABLE;
    if (!install) { set_phase("available", "Neue Firmware ist verfügbar."); goto done; }
    if (automatic_install && !app_ota_auto_version_allowed(manifest.version, history.blocked_version)) {
        set_phase("available", "Diese Version wird nach einem fehlgeschlagenen Start automatisch übersprungen. Manuelle Installation bleibt möglich.");
        goto done;
    }
    if (automatic_install && (!clock_valid() || settings.mode != APP_OTA_AUTO_INSTALL ||
        (settings.schedule == APP_OTA_SCHEDULE_DAILY &&
         !app_ota_install_due(&settings, (int64_t) time(NULL), 0)))) {
        set_phase("available", "Neue Firmware wartet auf die nächste gültige Installationszeit.");
        goto done;
    }
    if (!automatic_install && strcmp(manifest.version, s_requested_version) != 0) {
        result = APP_OTA_RESULT_INSTALL_FAILED;
        failed = true;
        set_error(ESP_ERR_INVALID_STATE, "Das Release hat sich geändert. Bitte erneut nach Updates suchen.");
        goto done;
    }
    failed = true;
    result = APP_OTA_RESULT_INSTALL_FAILED;
    set_phase("downloading", "Firmware wird heruntergeladen. Stromversorgung angeschlossen lassen.");
    err = download_image(&manifest);
    if (err != ESP_OK) {
        set_error(err, "Update fehlgeschlagen. Die bisherige Firmware bleibt aktiv.");
        goto done;
    }
    /* Persist the attempted version before selecting it. The previous firmware
       can then quarantine a failed/aborted first boot instead of retrying forever. */
    int64_t checked_at = clock_valid() ? (int64_t) time(NULL) : 0;
    portENTER_CRITICAL(&s_status_lock);
    strcpy(s_history.pending_version, manifest.version);
    s_history.checked_at = checked_at;
    portEXIT_CRITICAL(&s_status_lock);
    if (!persist_history()) {
        portENTER_CRITICAL(&s_status_lock);
        s_history.pending_version[0] = '\0';
        portEXIT_CRITICAL(&s_status_lock);
        set_error(ESP_FAIL, "Update-Verlauf konnte nicht gespeichert werden. Installation wurde angehalten.");
        goto done;
    }
    err = esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_history.pending_version[0] = '\0';
        portEXIT_CRITICAL(&s_status_lock);
        set_error(err, "Update konnte nicht zum Starten ausgewählt werden.");
        goto done;
    }
    set_phase("rebooting", "Update geprüft. Die Ampel startet neu.");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
done:
    finish_operation(result, failed);
    vTaskDelete(NULL);
}

void app_ota_init(void)
{
    esp_err_t load_err = app_ota_storage_load(&s_settings, &s_history);
    s_storage_ok = load_err == ESP_OK;
    if (!s_storage_ok) ESP_LOGE(TAG, "Update-Automatik konnte nicht geladen werden: %s", esp_err_to_name(load_err));
    uint32_t physical_size = 0;
    const esp_partition_t *ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    s_status.configured = app_security_ready() && esp_flash_get_physical_size(NULL, &physical_size) == ESP_OK && physical_size >= 0x400000 &&
                          ota0 != NULL && ota1 != NULL && data != NULL &&
                          ota0->address == 0x20000 && ota1->address == 0x200000 &&
                          ota0->size == OTA_SLOT_SIZE && ota1->size == OTA_SLOT_SIZE && data->size == 0x2000;
    esp_ota_img_states_t state;
    s_status.boot_pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                            state == ESP_OTA_IMG_PENDING_VERIFY;
    if (!s_status.configured) set_phase("disabled", "OTA-Partitionierung fehlt. Einmal vollständig per USB flashen.");
    else if (s_status.boot_pending) set_phase("boot_validation", "Neue Firmware wird beim Start geprüft.");
    if (s_storage_ok && s_status.configured && s_history.pending_version[0] &&
        strcmp(s_history.pending_version, esp_app_get_description()->version) != 0) finalize_startup_history();
}

static void boot_validation_task(void *argument)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Startprüfung konnte nicht bestätigt werden: %s", esp_err_to_name(err));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    } else {
        esp_err_t policy_err = app_security_apply_key_policy();
        if (policy_err != ESP_OK) ESP_LOGE(TAG, "Signierschluesselwechsel fehlgeschlagen: %s", esp_err_to_name(policy_err));
        finalize_startup_history();
        portENTER_CRITICAL(&s_status_lock);
        s_status.boot_pending = false;
        s_status.phase = "idle";
        s_status.message = "Startprüfung erfolgreich. Firmware bestätigt.";
        portEXIT_CRITICAL(&s_status_lock);
    }
    vTaskDelete(NULL);
}

void app_ota_confirm_startup(bool web_server_ready)
{
    if (s_boot_validation_started) return;
    if (!s_status.boot_pending) {
        if (web_server_ready && s_status.configured) {
            s_boot_validation_started = true;
            esp_err_t err = app_security_apply_key_policy();
            if (err != ESP_OK) ESP_LOGE(TAG, "Signierschluesselwechsel fehlgeschlagen: %s", esp_err_to_name(err));
            finalize_startup_history();
        }
        return;
    }
    if (!web_server_ready || !s_status.configured ||
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) < 1024 * 1024 ||
        xTaskCreate(boot_validation_task, "ota_boot_check", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Startprüfung fehlgeschlagen; kehre zur vorherigen Firmware zurück");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }
    s_boot_validation_started = true;
}

static esp_err_t begin_operation(ota_request_t kind, const char *version, uint32_t revision)
{
    bool install = kind == OTA_MANUAL_INSTALL;
    bool automatic = kind == OTA_AUTO_CHECK || kind == OTA_AUTO_INSTALL;
    bool valid_time = clock_valid();
    portENTER_CRITICAL(&s_status_lock);
    bool allowed = s_status.configured && !s_status.busy && !s_status.boot_pending &&
                   (!install || (strcmp(s_status.phase, "available") == 0 && version != NULL &&
                                 strcmp(s_status.latest_version, version) == 0)) &&
                   (!automatic || (s_storage_ok && revision == s_settings_revision && s_settings.mode != APP_OTA_AUTO_OFF)) &&
                   (kind != OTA_AUTO_INSTALL || (valid_time && s_settings.mode == APP_OTA_AUTO_INSTALL));
    if (allowed) {
        if (install) strcpy(s_requested_version, version);
        s_status.busy = true;
        s_status.phase = "checking";
        s_status.message = "GitHub-Release wird geprüft.";
        s_status.bytes = 0;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(update_task, "ota_update", 12288, (void *) (uintptr_t) kind, 3, NULL) != pdPASS) {
        set_error(ESP_ERR_NO_MEM, "Nicht genug Speicher für das Update.");
        finish_operation(APP_OTA_RESULT_CHECK_FAILED, true);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void time_synchronized(struct timeval *tv)
{
    portENTER_CRITICAL(&s_status_lock);
    s_clock_synced = app_timezone_time_supported(tv->tv_sec);
    portEXIT_CRITICAL(&s_status_lock);
}

static void automation_task(void *argument)
{
    (void) argument;
    bool ntp_started = false;
    for (;;) {
        if (!ntp_started) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            config.wait_for_sync = false;
            config.sync_cb = time_synchronized;
            esp_err_t err = esp_netif_sntp_init(&config);
            ntp_started = err == ESP_OK;
            if (!ntp_started) ESP_LOGW(TAG, "Internetzeit konnte nicht gestartet werden: %s", esp_err_to_name(err));
        }
        app_ota_settings_t settings;
        app_ota_history_t history;
        ota_status_t status;
        int64_t check_due, start;
        bool storage_ok;
        uint32_t revision;
        portENTER_CRITICAL(&s_status_lock);
        settings = s_settings;
        history = s_history;
        status = s_status;
        check_due = s_next_check_us;
        start = s_automation_start_us;
        storage_ok = s_storage_ok;
        revision = s_settings_revision;
        portEXIT_CRITICAL(&s_status_lock);
        int64_t now_us = esp_timer_get_time();
        wifi_ap_record_t access_point;
        bool connected = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
        if (connected && storage_ok && status.configured && !status.busy && !status.boot_pending &&
            settings.mode != APP_OTA_AUTO_OFF && now_us - start >= (int64_t) APP_OTA_STARTUP_DELAY_SECONDS * 1000000) {
            bool install = false;
            if (settings.mode == APP_OTA_AUTO_INSTALL && clock_valid()) {
                if (settings.schedule == APP_OTA_SCHEDULE_DAILY) {
                    install = app_ota_install_due(&settings, (int64_t) time(NULL), history.install_day);
                } else {
                    install = now_us >= check_due || (strcmp(status.phase, "available") == 0 &&
                              app_ota_auto_version_allowed(status.latest_version, history.blocked_version));
                }
            }
            if (install) begin_operation(OTA_AUTO_INSTALL, NULL, revision);
            else if (now_us >= check_due) begin_operation(OTA_AUTO_CHECK, NULL, revision);
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void app_ota_start_automation(void)
{
    if (s_automation_started || !s_status.configured) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_status_lock);
    s_automation_start_us = now;
    s_next_check_us = now + (int64_t) APP_OTA_STARTUP_DELAY_SECONDS * 1000000;
    s_automation_started = true;
    portEXIT_CRITICAL(&s_status_lock);
    if (xTaskCreate(automation_task, "ota_automatic", 4096, NULL, 2, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_status_lock);
        s_automation_started = false;
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGE(TAG, "Update-Automatik konnte nicht gestartet werden");
    }
}

static cJSON *automation_status_json(void)
{
    app_ota_settings_t settings;
    app_ota_history_t history;
    int64_t due;
    bool storage_ok, started;
    char latest[OTA_VERSION_LIMIT];
    portENTER_CRITICAL(&s_status_lock);
    settings = s_settings;
    history = s_history;
    due = s_next_check_us;
    storage_ok = s_storage_ok;
    started = s_automation_started;
    strcpy(latest, s_status.latest_version);
    portEXIT_CRITICAL(&s_status_lock);
    bool synced = clock_valid();
    int64_t now = synced ? (int64_t) time(NULL) : 0;
    int64_t remaining = due - esp_timer_get_time();
    if (remaining < 0) remaining = 0;
    char install_time[6];
    unsigned minute = settings.install_minute % 1440;
    snprintf(install_time, sizeof(install_time), "%02u:%02u", minute / 60, minute % 60);
    static const char *const modes[] = {"off", "check", "install"};
    static const char *const results[] = {"never", "current", "available", "check_failed", "install_failed", "installed", "rolled_back"};
    int64_t next_install = synced ? app_ota_next_install(&settings, now, history.install_day) : 0;
    char local_time[24] = {0}, checked_local[24] = {0}, install_local[24] = {0}, installed_local[24] = {0};
    const char *zone = settings.timezone[0] ? settings.timezone : "Etc/UTC";
    struct tm local;
    if (now && app_timezone_localtime(zone, now, &local)) strftime(local_time, sizeof(local_time), "%Y-%m-%dT%H:%M:%S", &local);
    if (history.checked_at && app_timezone_localtime(zone, history.checked_at, &local)) strftime(checked_local, sizeof(checked_local), "%Y-%m-%dT%H:%M:%S", &local);
    if (next_install && app_timezone_localtime(zone, next_install, &local)) strftime(install_local, sizeof(install_local), "%Y-%m-%dT%H:%M:%S", &local);
    if (history.installed_at && app_timezone_localtime(zone, history.installed_at, &local)) strftime(installed_local, sizeof(installed_local), "%Y-%m-%dT%H:%M:%S", &local);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "mode", modes[settings.mode]) == NULL ||
        cJSON_AddNumberToObject(json, "interval_hours", settings.interval_hours) == NULL ||
        cJSON_AddStringToObject(json, "schedule", settings.schedule == APP_OTA_SCHEDULE_DAILY ? "daily" : "immediate") == NULL ||
        cJSON_AddStringToObject(json, "install_time", install_time) == NULL ||
        cJSON_AddStringToObject(json, "time_zone", settings.timezone) == NULL ||
        cJSON_AddBoolToObject(json, "timezone_auto", settings.timezone_auto) == NULL ||
        cJSON_AddBoolToObject(json, "clock_synced", synced) == NULL ||
        cJSON_AddBoolToObject(json, "storage_ok", storage_ok) == NULL ||
        cJSON_AddBoolToObject(json, "running", started) == NULL ||
        cJSON_AddNumberToObject(json, "device_time", now) == NULL ||
        cJSON_AddStringToObject(json, "local_time", local_time) == NULL ||
        cJSON_AddStringToObject(json, "last_check_local", checked_local) == NULL ||
        cJSON_AddStringToObject(json, "next_install_local", install_local) == NULL ||
        cJSON_AddStringToObject(json, "last_install_local", installed_local) == NULL ||
        cJSON_AddNumberToObject(json, "last_check_at", history.checked_at) == NULL ||
        cJSON_AddNumberToObject(json, "next_check_in_seconds", started && settings.mode != APP_OTA_AUTO_OFF ? (remaining + 999999) / 1000000 : 0) == NULL ||
        cJSON_AddNumberToObject(json, "next_install_at", next_install) == NULL ||
        cJSON_AddStringToObject(json, "last_result", results[history.result]) == NULL ||
        cJSON_AddStringToObject(json, "last_installed_version", history.installed_version) == NULL ||
        cJSON_AddNumberToObject(json, "last_install_at", history.installed_at) == NULL ||
        cJSON_AddStringToObject(json, "blocked_version", history.blocked_version) == NULL ||
        cJSON_AddBoolToObject(json, "version_blocked", latest[0] && history.blocked_version[0] &&
                              !app_ota_auto_version_allowed(latest, history.blocked_version)) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

esp_err_t app_ota_status_handler(httpd_req_t *request)
{
    ota_status_t status;
    portENTER_CRITICAL(&s_status_lock);
    status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "phase", status.phase) == NULL ||
        cJSON_AddStringToObject(json, "message", status.message) == NULL ||
        cJSON_AddStringToObject(json, "current_version", esp_app_get_description()->version) == NULL ||
        cJSON_AddStringToObject(json, "latest_version", status.latest_version) == NULL ||
        cJSON_AddStringToObject(json, "repository", OTA_REPOSITORY) == NULL ||
        cJSON_AddNumberToObject(json, "bytes", status.bytes) == NULL ||
        cJSON_AddNumberToObject(json, "total", status.total) == NULL ||
        cJSON_AddBoolToObject(json, "busy", status.busy) == NULL ||
        cJSON_AddBoolToObject(json, "boot_pending", status.boot_pending) == NULL ||
        cJSON_AddBoolToObject(json, "configured", status.configured) == NULL) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Nicht genug Speicher.");
    }
    cJSON *automation = automation_status_json();
    if (automation == NULL || !cJSON_AddItemToObject(json, "automation", automation)) {
        cJSON_Delete(automation);
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    cJSON *security = app_security_status_json();
    if (security == NULL || !cJSON_AddItemToObject(json, "security", security)) {
        cJSON_Delete(security);
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

static esp_err_t start_update(httpd_req_t *request, bool install)
{
    char content_type[48];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0 || request->content_len <= 0 || request->content_len >= 128) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "JSON-Anfrage erforderlich.");
    }
    char body[128] = {0};
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Anfrage unvollständig.");
        received += count;
    }
    cJSON *json = memchr(body, '\0', received) == NULL ? cJSON_ParseWithOpts(body, NULL, true) : NULL;
    const char *version = json_string(json, "version");
    uint32_t parts[3];
    if (!cJSON_IsObject(json) || (install && !app_ota_parse_version(version, parts))) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Ungültige Firmware-Version.");
    }
    esp_err_t err = begin_operation(install ? OTA_MANUAL_INSTALL : OTA_MANUAL_CHECK, version, 0);
    cJSON_Delete(json);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Update beschäftigt, noch nicht geprüft oder nicht eingerichtet.");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Nicht genug Speicher.");
    }
    httpd_resp_set_status(request, "202 Accepted");
    return app_ota_status_handler(request);
}

esp_err_t app_ota_check_handler(httpd_req_t *request) { return start_update(request, false); }
esp_err_t app_ota_install_handler(httpd_req_t *request) { return start_update(request, true); }

esp_err_t app_ota_timezones_handler(httpd_req_t *request)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *zones = json == NULL ? NULL : cJSON_AddArrayToObject(json, "zones");
    if (zones == NULL) { cJSON_Delete(json); return ESP_ERR_NO_MEM; }
    for (size_t index = 0; index < app_timezone_count(); ++index) {
        cJSON *name = cJSON_CreateString(app_timezone_name(index));
        if (name == NULL || !cJSON_AddItemToArray(zones, name)) {
            cJSON_Delete(name);
            cJSON_Delete(json);
            return ESP_ERR_NO_MEM;
        }
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

esp_err_t app_ota_settings_handler(httpd_req_t *request)
{
    char content_type[48], body[512] = {0};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0 || request->content_len <= 0 ||
        request->content_len >= sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "JSON-Anfrage erforderlich.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Anfrage unvollständig.");
        received += count;
    }
    cJSON *json = memchr(body, '\0', received) == NULL ? cJSON_ParseWithOpts(body, NULL, true) : NULL;
    const char *mode = json_string(json, "mode");
    const char *schedule = json_string(json, "schedule");
    const char *install_time = json_string(json, "install_time");
    const char *zone = json_string(json, "time_zone");
    const cJSON *interval = cJSON_GetObjectItemCaseSensitive(json, "interval_hours");
    const cJSON *automatic_zone = cJSON_GetObjectItemCaseSensitive(json, "timezone_auto");
    app_ota_settings_t settings;
    app_ota_settings_defaults(&settings);
    bool valid = cJSON_IsObject(json) && mode != NULL && schedule != NULL && zone != NULL &&
                 strlen(zone) < sizeof(settings.timezone) && cJSON_IsNumber(interval) &&
                 (interval->valuedouble == 1 || interval->valuedouble == 6 ||
                  interval->valuedouble == 12 || interval->valuedouble == 24) &&
                 cJSON_IsBool(automatic_zone) && install_time != NULL && strlen(install_time) == 5 &&
                 install_time[0] >= '0' && install_time[0] <= '2' &&
                 install_time[1] >= '0' && install_time[1] <= '9' && install_time[2] == ':' &&
                 install_time[3] >= '0' && install_time[3] <= '5' &&
                 install_time[4] >= '0' && install_time[4] <= '9';
    if (valid) {
        settings.mode = strcmp(mode, "off") == 0 ? APP_OTA_AUTO_OFF :
                        strcmp(mode, "check") == 0 ? APP_OTA_AUTO_CHECK :
                        strcmp(mode, "install") == 0 ? APP_OTA_AUTO_INSTALL : UINT8_MAX;
        settings.schedule = strcmp(schedule, "daily") == 0 ? APP_OTA_SCHEDULE_DAILY :
                            strcmp(schedule, "immediate") == 0 ? APP_OTA_SCHEDULE_IMMEDIATE : UINT8_MAX;
        settings.interval_hours = (uint8_t) interval->valuedouble;
        settings.install_minute = ((install_time[0] - '0') * 10 + install_time[1] - '0') * 60 +
                                  (install_time[3] - '0') * 10 + install_time[4] - '0';
        settings.timezone_auto = cJSON_IsTrue(automatic_zone);
        strcpy(settings.timezone, zone);
        valid = app_ota_settings_valid(&settings);
    }
    cJSON_Delete(json);
    if (!valid) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Ungültige Update-Einstellungen. Uhrzeit und Zeitzone prüfen.");
    app_ota_history_t history;
    portENTER_CRITICAL(&s_status_lock);
    bool allowed = !s_status.busy && !s_status.boot_pending;
    if (allowed) s_status.busy = true;
    history = s_history;
    if (s_settings.install_minute != settings.install_minute || s_settings.schedule != settings.schedule ||
        strcmp(s_settings.timezone, settings.timezone) != 0) history.install_day = 0;
    portEXIT_CRITICAL(&s_status_lock);
    if (!allowed) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Ein Update läuft oder wird beim Start geprüft. Bitte danach speichern.");
    }
    esp_err_t err = app_ota_storage_save_settings(&settings, &history);
    int64_t next_check = esp_timer_get_time() + (int64_t) APP_OTA_STARTUP_DELAY_SECONDS * 1000000;
    portENTER_CRITICAL(&s_status_lock);
    s_storage_ok = err == ESP_OK;
    if (err == ESP_OK) {
        s_settings = settings;
        s_history = history;
        ++s_settings_revision;
        s_next_check_us = next_check;
        s_failures = 0;
    }
    s_status.busy = false;
    portEXIT_CRITICAL(&s_status_lock);
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Update-Einstellungen konnten nicht gespeichert werden.");
    return app_ota_status_handler(request);
}

extern const unsigned char ota_js_start[] asm("_binary_ota_js_start");
extern const unsigned char ota_js_end[] asm("_binary_ota_js_end");

esp_err_t app_ota_script_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(request, (const char *) ota_js_start, ota_js_end - ota_js_start - 1);
}
