#include "web/web_server.h"

#include "config/config_manager.h"
#include "wifi/wifi_manager.h"
#include "actuators/motor_controller.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG              "web"
#define POST_BODY_MAX    512
#define CTRL_BUF_SIZE    4096

static httpd_handle_t   s_server     = NULL;
static motor_ctrl_cb_t  s_motor_cb   = NULL;
static SemaphoreHandle_t s_mutex     = NULL;

static struct {
    float moisture_pct;
    bool  motor_on;
    bool  motor_manual;
    char  device_name[DEVICE_NAME_MAX_LEN];
} s_status;

/* ---------- helpers ---------- */

static void url_decode(char *str) {
    char *r = str, *w = str;
    while (*r) {
        if (*r == '+') {
            *w++ = ' '; r++;
        } else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = {r[1], r[2], '\0'};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static bool form_field(const char *body, const char *key, char *out, size_t out_len) {
    char search[80];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    url_decode(out);
    return true;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_len) {
    int total = 0;
    int remaining = (int)req->content_len;
    if (remaining >= (int)buf_len) remaining = (int)buf_len - 1;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf + total, remaining);
        if (n <= 0) return ESP_FAIL;
        total += n;
        remaining -= n;
    }
    buf[total] = '\0';
    return ESP_OK;
}

/* ---------- setup page HTML (served from rodata in flash) ---------- */

static const char SETUP_HTML[] =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>Onnion Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:system-ui,sans-serif;background:#e8f5e9;"
    "min-height:100vh;display:flex;align-items:center;justify-content:center}"
    ".card{background:#fff;border-radius:12px;padding:32px;width:100%;"
    "max-width:440px;box-shadow:0 4px 16px rgba(0,0,0,.12)}"
    "h1{color:#2e7d32;margin-bottom:24px;font-size:1.4rem}"
    "label{display:block;margin-top:14px;font-size:.85rem;font-weight:600;color:#444}"
    "input,select{display:block;width:100%;padding:10px 12px;margin-top:5px;"
    "border:1px solid #ccc;border-radius:6px;font-size:1rem;background:#fff}"
    "input:focus,select:focus{outline:none;border-color:#43a047;"
    "box-shadow:0 0 0 2px rgba(67,160,71,.25)}"
    ".hint{font-size:.75rem;color:#888;margin-top:4px}"
    "button[type=submit]{display:block;width:100%;padding:13px;margin-top:24px;"
    "background:#43a047;color:#fff;border:none;border-radius:6px;"
    "font-size:1rem;font-weight:600;cursor:pointer}"
    "button[type=submit]:hover{background:#2e7d32}"
    ".btn-scan{display:block;width:100%;padding:8px;margin-top:6px;"
    "background:#607d8b;color:#fff;border:none;border-radius:6px;"
    "font-size:.85rem;cursor:pointer}"
    ".btn-scan:hover{background:#455a64}"
    "</style></head>"
    "<body><div class=\"card\">"
    "<h1>Onnion Device Setup</h1>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>Device Name</label>"
    "<input type=\"text\" name=\"device_name\" placeholder=\"my-plant\""
    " maxlength=\"63\" required>"
    "<label>WiFi Network</label>"
    "<select id=\"ssid_sel\"><option>Scanning...</option></select>"
    "<p class=\"hint\" id=\"scan_hint\">Scanning for networks...</p>"
    "<button type=\"button\" class=\"btn-scan\" onclick=\"doScan()\">"
    "&#8635; Rescan</button>"
    "<input type=\"text\" name=\"wifi_ssid\" id=\"ssid_txt\""
    " placeholder=\"or type SSID manually\" maxlength=\"63\" required"
    " style=\"margin-top:8px\">"
    "<label>WiFi Password</label>"
    "<input type=\"password\" name=\"wifi_pass\" placeholder=\"(leave blank if open)\""
    " maxlength=\"63\">"
    "<label>MQTT Broker URL</label>"
    "<input type=\"text\" name=\"mqtt_url\""
    " placeholder=\"mqtt://broker.example.com:1883\" maxlength=\"255\" required>"
    "<p class=\"hint\">Use mqtt:// for plain, mqtts:// for TLS</p>"
    "<label>Moisture Setpoint (%)</label>"
    "<input type=\"number\" name=\"setpoint\" value=\"60\" min=\"0\" max=\"100\">"
    "<p class=\"hint\">Target soil moisture (PID setpoint)</p>"
    "<button type=\"submit\">Save &amp; Connect</button>"
    "</form>"
    "<script>"
    "var sel=document.getElementById('ssid_sel'),"
    "txt=document.getElementById('ssid_txt');"
    "sel.onchange=function(){txt.value=this.value;};"
    "function doScan(){"
    "sel.innerHTML='<option>Scanning...</option>';"
    "txt.value='';"
    "document.getElementById('scan_hint').textContent='Scanning...';"
    "fetch('/scan').then(function(r){return r.json();})"
    ".then(function(d){"
    "sel.innerHTML='';"
    "if(!d.ssids||!d.ssids.length){"
    "sel.innerHTML='<option>No networks found</option>';"
    "document.getElementById('scan_hint').textContent='None found – type SSID below.';"
    "return;}"
    "d.ssids.forEach(function(s){"
    "var o=document.createElement('option');o.value=s;o.textContent=s;"
    "sel.appendChild(o);});"
    "txt.value=sel.value;"
    "document.getElementById('scan_hint').textContent=d.ssids.length+' network(s) found';"
    "}).catch(function(){"
    "document.getElementById('scan_hint').textContent='Scan failed – type SSID below.';});"
    "}"
    "doScan();"
    "</script>"
    "</div></body></html>";

static const char SAVED_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>Saved</title>"
    "<style>body{font-family:system-ui,sans-serif;display:flex;align-items:center;"
    "justify-content:center;min-height:100vh;background:#e8f5e9}"
    ".msg{background:#fff;padding:32px;border-radius:12px;text-align:center;"
    "box-shadow:0 4px 16px rgba(0,0,0,.12)}"
    "h2{color:#2e7d32}p{margin-top:12px;color:#555;line-height:1.5}"
    "</style></head><body>"
    "<div class=\"msg\"><h2>Config Saved!</h2>"
    "<p>Device is restarting and will join your WiFi.<br>"
    "You can close this page.</p></div></body></html>";

/* ---------- control page (built dynamically into heap buffer) ---------- */

static void build_control_page(char *buf, size_t buf_len) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    float moisture  = s_status.moisture_pct;
    bool  motor_on  = s_status.motor_on;
    bool  manual    = s_status.motor_manual;
    char  name[DEVICE_NAME_MAX_LEN];
    strncpy(name, s_status.device_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    xSemaphoreGive(s_mutex);

    const char *motor_str = motor_on ? "ON"       : "OFF";
    const char *mode_str  = manual   ? "Manual"   : "Auto (PID)";
    const char *btn_class = motor_on ? "btn-red"  : "btn-green";
    const char *btn_label = motor_on ? "Turn OFF" : "Turn ON";

    /* Format specifiers in order: %s %s %.1f %s %s %s %s
       The %% sequences inside CSS produce a literal % in the output. */
    snprintf(buf, buf_len,
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
        "<meta http-equiv=\"refresh\" content=\"5\">"
        "<title>%s</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:system-ui,sans-serif;background:#e8f5e9;padding:20px}"
        "h1{color:#2e7d32;margin-bottom:18px}"
        ".card{background:#fff;border-radius:10px;padding:20px;margin-bottom:14px;"
        "box-shadow:0 2px 8px rgba(0,0,0,.08)}"
        ".card h2{font-size:.9rem;color:#777;margin-bottom:8px;text-transform:uppercase;"
        "letter-spacing:.05em}"
        ".value{font-size:2.2rem;font-weight:700;color:#2e7d32}"
        ".meta{font-size:.8rem;color:#aaa;margin-top:4px}"
        ".btn{display:block;width:100%%;padding:13px;border:none;border-radius:6px;"
        "font-size:1rem;font-weight:600;cursor:pointer;text-align:center;"
        "text-decoration:none}"
        ".btn-green{background:#43a047;color:#fff}"
        ".btn-red{background:#e53935;color:#fff}"
        ".btn-gray{background:#9e9e9e;color:#fff}"
        "</style></head>"
        "<body>"
        "<h1>%s</h1>"
        "<div class=\"card\">"
        "<h2>Soil Moisture</h2>"
        "<div class=\"value\">%.1f%%</div>"
        "<div class=\"meta\">Refreshes every 5 s</div>"
        "</div>"
        "<div class=\"card\">"
        "<h2>Motor (GPIO 4)</h2>"
        "<div class=\"value\">%s</div>"
        "<div class=\"meta\">Mode: %s</div>"
        "<button id=\"mbtn\" class=\"btn %s\" onclick=\"toggleMotor()\""
        " style=\"margin-top:14px\">%s</button>"
        "<a class=\"btn btn-gray\" href=\"/auto\""
        " style=\"margin-top:8px;padding:11px;display:block\">"
        "Return to Auto (PID)</a>"
        "</div>"
        "<div class=\"card\">"
        "<form method=\"POST\" action=\"/reset\">"
        "<button class=\"btn btn-gray\" type=\"submit\">Reset to Setup</button>"
        "</form>"
        "</div>"
        "<script>"
        "function toggleMotor(){"
        "var b=document.getElementById('mbtn');"
        "b.disabled=true;"
        "fetch('/motor',{method:'POST'})"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
        "b.className='btn '+(d.on?'btn-red':'btn-green');"
        "b.textContent=d.on?'Turn OFF':'Turn ON';"
        "b.disabled=false;})"
        ".catch(function(){b.disabled=false;});}"
        "</script>"
        "</body></html>",
        name,        /* title   */
        name,        /* h1      */
        moisture,    /* %.1f%%  */
        motor_str,   /* value   */
        mode_str,    /* meta    */
        btn_class,   /* class   */
        btn_label    /* label   */
    );
}

/* ---------- WiFi scan handler (setup mode only) ---------- */

static void json_escape(const char *src, char *dst, size_t dst_len) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 2 < dst_len; i++) {
        if (src[i] == '"' || src[i] == '\\') dst[di++] = '\\';
        dst[di++] = src[i];
    }
    dst[di] = '\0';
}

static esp_err_t h_scan_get(httpd_req_t *req) {
    char ssids[WIFI_SCAN_MAX][33];
    uint16_t count = 0;
    wifi_scan_ap(ssids, &count, WIFI_SCAN_MAX);

    char buf[768];
    int pos = snprintf(buf, sizeof(buf), "{\"ssids\":[");
    for (uint16_t i = 0; i < count && pos < (int)sizeof(buf) - 70; i++) {
        char esc[66];
        json_escape(ssids[i], esc, sizeof(esc));
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s\"%s\"", i > 0 ? "," : "", esc);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    return ESP_OK;
}

/* ---------- captive-portal redirect (setup mode only) ---------- */

/* Any URL not registered (Android /generate_204, Apple /hotspot-detect.html,
   Windows /ncsi.txt, etc.) gets a 302 back to the setup page.
   This triggers the "Sign in to network" popup on every major OS. */
static esp_err_t h_captive_404(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------- setup handlers ---------- */

static esp_err_t h_setup_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_setup_save(httpd_req_t *req) {
    char body[POST_BODY_MAX];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    device_config_t cfg = {0};

    if (!form_field(body, "device_name", cfg.device_name, sizeof(cfg.device_name)) ||
        !form_field(body, "wifi_ssid",   cfg.wifi_ssid,   sizeof(cfg.wifi_ssid))   ||
        !form_field(body, "mqtt_url",    cfg.mqtt_url,    sizeof(cfg.mqtt_url))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }

    form_field(body, "wifi_pass", cfg.wifi_password, sizeof(cfg.wifi_password));

    char sp[8] = "60";
    form_field(body, "setpoint", sp, sizeof(sp));
    cfg.pid_setpoint = (float)atof(sp);
    cfg.pid_kp = 2.0f;
    cfg.pid_ki = 0.1f;
    cfg.pid_kd = 0.5f;

    config_save(&cfg);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

/* ---------- control handlers ---------- */

static esp_err_t h_control_get(httpd_req_t *req) {
    char *buf = malloc(CTRL_BUF_SIZE);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
    build_control_page(buf, CTRL_BUF_SIZE);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static esp_err_t h_motor_post(httpd_req_t *req) {
    /* Toggle: flip current motor state and lock into manual mode. */
    bool new_on = !motor_is_on();
    if (s_motor_cb) s_motor_cb(new_on, true);

    char resp[32];
    snprintf(resp, sizeof(resp), "{\"on\":%s}", motor_is_on() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_auto_get(httpd_req_t *req) {
    if (s_motor_cb) s_motor_cb(false, false);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_reset_post(httpd_req_t *req) {
    static const char msg[] =
        "<html><body><p>Resetting device to setup mode...</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);

    config_erase();
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

/* ---------- public API ---------- */

void web_server_start_setup(void) {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/",      .method = HTTP_GET,  .handler = h_setup_get},
        {.uri = "/save",  .method = HTTP_POST, .handler = h_setup_save},
        {.uri = "/scan",  .method = HTTP_GET,  .handler = h_scan_get},
    };
    for (int i = 0; i < 3; i++) httpd_register_uri_handler(s_server, &routes[i]);

    /* Redirect every unrecognised URL (OS captive-portal probes) to setup page. */
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_captive_404);

    ESP_LOGI(TAG, "Setup server + captive portal ready at http://192.168.4.1");
}

void web_server_start_control(const char *device_name, motor_ctrl_cb_t motor_cb) {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    s_motor_cb = motor_cb;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_status.device_name, device_name, sizeof(s_status.device_name) - 1);
    xSemaphoreGive(s_mutex);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/",      .method = HTTP_GET,  .handler = h_control_get},
        {.uri = "/motor", .method = HTTP_POST, .handler = h_motor_post},
        {.uri = "/auto",  .method = HTTP_GET,  .handler = h_auto_get},
        {.uri = "/reset", .method = HTTP_POST, .handler = h_reset_post},
    };
    for (int i = 0; i < 4; i++) httpd_register_uri_handler(s_server, &routes[i]);

    ESP_LOGI(TAG, "Control server ready");
}

void web_server_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

void web_server_update_status(float moisture_pct, bool motor_on, bool motor_manual) {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.moisture_pct = moisture_pct;
    s_status.motor_on     = motor_on;
    s_status.motor_manual = motor_manual;
    xSemaphoreGive(s_mutex);
}
