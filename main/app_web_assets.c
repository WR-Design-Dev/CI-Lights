#include "app_web_assets.h"
#include "app_branding.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

extern const char traffic_light_svg_start[] asm("_binary_TrafficLight_svg_start");
extern const char traffic_light_svg_end[] asm("_binary_TrafficLight_svg_end");
extern const char localization_js_start[] asm("_binary_localization_js_start");
extern const char localization_js_end[] asm("_binary_localization_js_end");

static const char *asset_content_type(app_branding_format_t format)
{
    switch (format) {
        case APP_BRANDING_SVG: return "image/svg+xml";
        case APP_BRANDING_PNG: return "image/png";
        case APP_BRANDING_ICO: return "image/x-icon";
        default: return "application/octet-stream";
    }
}

static const char *asset_format_name(app_branding_format_t format)
{
    switch (format) {
        case APP_BRANDING_SVG: return "svg";
        case APP_BRANDING_PNG: return "png";
        case APP_BRANDING_ICO: return "ico";
        default: return "";
    }
}

static esp_err_t send_branding_asset(httpd_req_t *request, app_branding_kind_t kind)
{
    app_branding_info_t info;
    if (!app_branding_get_info(kind, &info)) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Branding not uploaded");
    }
    void *data = heap_caps_malloc_prefer(info.length, 2,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (data == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }
    esp_err_t err = app_branding_read(kind, data, info.length);
    if (err == ESP_OK) {
        httpd_resp_set_type(request, asset_content_type(info.format));
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
        if (info.format == APP_BRANDING_SVG) {
            httpd_resp_set_hdr(request, "Content-Security-Policy",
                               "sandbox; default-src 'none'; style-src 'unsafe-inline'");
        }
        err = httpd_resp_send(request, data, info.length);
    } else {
        err = httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                  "Branding could not be read");
    }
    heap_caps_free(data);
    return err;
}

esp_err_t app_web_logo_handler(httpd_req_t *request)
{
    return send_branding_asset(request, APP_BRANDING_LOGO);
}

esp_err_t app_web_traffic_light_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "image/svg+xml");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(request, traffic_light_svg_start,
                           traffic_light_svg_end - traffic_light_svg_start - 1);
}

esp_err_t app_web_favicon_handler(httpd_req_t *request)
{
    return send_branding_asset(request, APP_BRANDING_FAVICON);
}

esp_err_t app_web_localization_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/javascript; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(request, localization_js_start,
                           localization_js_end - localization_js_start - 1);
}

esp_err_t app_web_branding_status_handler(httpd_req_t *request)
{
    app_branding_info_t logo = {0};
    app_branding_info_t favicon = {0};
    bool has_logo = app_branding_get_info(APP_BRANDING_LOGO, &logo);
    bool has_favicon = app_branding_get_info(APP_BRANDING_FAVICON, &favicon);
    char body[192];
    snprintf(body, sizeof(body),
             "{\"logo\":{\"uploaded\":%s,\"bytes\":%u,\"format\":\"%s\"},"
             "\"favicon\":{\"uploaded\":%s,\"bytes\":%u,\"format\":\"%s\"}}",
             has_logo ? "true" : "false", (unsigned) logo.length,
             asset_format_name(logo.format), has_favicon ? "true" : "false",
             (unsigned) favicon.length, asset_format_name(favicon.format));
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

static bool requested_kind(httpd_req_t *request, app_branding_kind_t *kind)
{
    char query[40];
    char name[16];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "kind", name, sizeof(name)) != ESP_OK) {
        return false;
    }
    if (strcmp(name, "logo") == 0) {
        *kind = APP_BRANDING_LOGO;
    } else if (strcmp(name, "favicon") == 0) {
        *kind = APP_BRANDING_FAVICON;
    } else {
        return false;
    }
    return true;
}

static bool asset_signature_valid(app_branding_format_t format,
                                  const uint8_t *data, size_t length)
{
    static const uint8_t png_header[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (format == APP_BRANDING_PNG) {
        return length >= sizeof(png_header) &&
               memcmp(data, png_header, sizeof(png_header)) == 0;
    }
    if (format == APP_BRANDING_ICO) {
        return length >= 6 && data[0] == 0 && data[1] == 0 &&
               data[2] == 1 && data[3] == 0 && (data[4] != 0 || data[5] != 0);
    }
    if (format == APP_BRANDING_SVG) {
        size_t prefix = length < 512 ? length : 512;
        for (size_t i = 0; i + 4 < prefix; ++i) {
            if (data[i] == '<' && memcmp(data + i, "<svg", 4) == 0 &&
                (data[i + 4] == ' ' || data[i + 4] == '\n' ||
                 data[i + 4] == '\r' || data[i + 4] == '\t' || data[i + 4] == '>')) {
                return true;
            }
        }
    }
    return false;
}

static esp_err_t branding_upload_error(httpd_req_t *request, const char *status,
                                       const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}

esp_err_t app_web_branding_upload_handler(httpd_req_t *request)
{
    app_branding_kind_t kind;
    if (!requested_kind(request, &kind)) {
        return branding_upload_error(request, "400 Bad Request", "Ungueltiger Branding-Typ.");
    }
    if (request->content_len <= 0 || request->content_len > APP_BRANDING_MAX_BYTES) {
        return branding_upload_error(request, "413 Content Too Large",
                                     "Datei ist zu gross (maximal 32 KiB).");
    }
    char content_type[48];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                     sizeof(content_type)) != ESP_OK) {
        return branding_upload_error(request, "400 Bad Request", "Dateityp fehlt.");
    }
    app_branding_format_t format = 0;
    if (kind == APP_BRANDING_LOGO && strcmp(content_type, "image/svg+xml") == 0) {
        format = APP_BRANDING_SVG;
    } else if (strcmp(content_type, "image/png") == 0) {
        format = APP_BRANDING_PNG;
    } else if (kind == APP_BRANDING_FAVICON &&
               (strcmp(content_type, "image/x-icon") == 0 ||
                strcmp(content_type, "image/vnd.microsoft.icon") == 0)) {
        format = APP_BRANDING_ICO;
    }
    if (format == 0) {
        return branding_upload_error(request, "415 Unsupported Media Type",
                                     "Logo: SVG/PNG; Favicon: ICO/PNG.");
    }
    uint8_t *data = heap_caps_malloc_prefer(request->content_len, 2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (data == NULL) {
        return branding_upload_error(request, "500 Internal Server Error",
                                     "Nicht genug Speicher fuer den Upload.");
    }
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, (char *) data + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            heap_caps_free(data);
            return branding_upload_error(request, "400 Bad Request",
                                         "Upload konnte nicht gelesen werden.");
        }
        received += count;
    }
    if (!asset_signature_valid(format, data, request->content_len)) {
        heap_caps_free(data);
        return branding_upload_error(request, "400 Bad Request",
                                     "Dateiinhalt passt nicht zum Format.");
    }
    esp_err_t err = app_branding_save(kind, format, data, request->content_len);
    heap_caps_free(data);
    if (err != ESP_OK) {
        return branding_upload_error(request, "500 Internal Server Error",
                                     "Branding konnte nicht gespeichert werden.");
    }
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, "Branding gespeichert.");
}
