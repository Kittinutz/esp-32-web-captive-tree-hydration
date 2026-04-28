#include "config/config_manager.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <string.h>

#define TAG           "config"
#define NVS_NAMESPACE "onnion"

static void save_float(nvs_handle_t h, const char *key, float val) {
    uint32_t bits;
    memcpy(&bits, &val, sizeof(bits));
    nvs_set_u32(h, key, bits);
}

static float load_float(nvs_handle_t h, const char *key, float fallback) {
    uint32_t bits = 0;
    if (nvs_get_u32(h, key, &bits) != ESP_OK) return fallback;
    float val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

void config_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

bool config_load(device_config_t *cfg) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    bool ok = true;
    size_t len;

    len = sizeof(cfg->device_name);
    ok &= (nvs_get_str(h, "device_name", cfg->device_name, &len) == ESP_OK);

    len = sizeof(cfg->wifi_ssid);
    ok &= (nvs_get_str(h, "wifi_ssid", cfg->wifi_ssid, &len) == ESP_OK);

    len = sizeof(cfg->wifi_password);
    if (nvs_get_str(h, "wifi_pass", cfg->wifi_password, &len) != ESP_OK) {
        cfg->wifi_password[0] = '\0';
    }

    len = sizeof(cfg->mqtt_url);
    ok &= (nvs_get_str(h, "mqtt_url", cfg->mqtt_url, &len) == ESP_OK);

    cfg->pid_setpoint = load_float(h, "pid_setpoint", 60.0f);
    cfg->pid_kp       = load_float(h, "pid_kp",        2.0f);
    cfg->pid_ki       = load_float(h, "pid_ki",        0.1f);
    cfg->pid_kd       = load_float(h, "pid_kd",        0.5f);

    nvs_close(h);
    return ok;
}

void config_save(const device_config_t *cfg) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed");
        return;
    }

    nvs_set_str(h, "device_name", cfg->device_name);
    nvs_set_str(h, "wifi_ssid",   cfg->wifi_ssid);
    nvs_set_str(h, "wifi_pass",   cfg->wifi_password);
    nvs_set_str(h, "mqtt_url",    cfg->mqtt_url);
    save_float(h, "pid_setpoint", cfg->pid_setpoint);
    save_float(h, "pid_kp",       cfg->pid_kp);
    save_float(h, "pid_ki",       cfg->pid_ki);
    save_float(h, "pid_kd",       cfg->pid_kd);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved: device=%s ssid=%s", cfg->device_name, cfg->wifi_ssid);
}

void config_erase(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Config erased");
    }
}

bool config_is_valid(const device_config_t *cfg) {
    return cfg->device_name[0] != '\0' &&
           cfg->wifi_ssid[0]   != '\0' &&
           cfg->mqtt_url[0]    != '\0';
}
