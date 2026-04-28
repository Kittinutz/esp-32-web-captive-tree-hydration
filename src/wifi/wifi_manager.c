#include "wifi/wifi_manager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

#define TAG              "wifi"
#define CONNECTED_BIT    BIT0
#define FAIL_BIT         BIT1
#define MAX_RETRY        5

static EventGroupHandle_t s_event_group;
static int s_retry = 0;

static void sta_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "Retry WiFi %d/%d", s_retry, MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_event_group, FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);
    }
}

void wifi_manager_init(void) {
    s_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
}

void wifi_start_ap(const char *ssid, const char *password) {
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t ap_cfg = {
        .ap = {
            .max_connection = 4,
            .authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ssid);
    if (password && password[0]) {
        strncpy((char *)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password) - 1);
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "AP started  SSID=%s  IP=192.168.4.1", ssid);
}

bool wifi_start_sta(const char *ssid, const char *password, int timeout_sec) {
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t h_any, h_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        sta_event_handler, NULL, &h_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        sta_event_handler, NULL, &h_ip);

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           CONNECTED_BIT | FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_sec * 1000));

    if (bits & CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected: %s", ssid);
        return true;
    }

    ESP_LOGE(TAG, "Connection failed: %s", ssid);
    return false;
}

bool wifi_is_connected(void) {
    return (xEventGroupGetBits(s_event_group) & CONNECTED_BIT) != 0;
}
