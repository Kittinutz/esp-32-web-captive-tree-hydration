#include "mqtt/mqtt_manager.h"

#include "mqtt_client.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#define TAG "mqtt"

static esp_mqtt_client_handle_t s_client    = NULL;
static bool                     s_connected = false;
static char s_topic_moisture[128];
static char s_topic_motor[128];

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            s_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            s_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error");
            break;
        default:
            break;
    }
}

bool mqtt_manager_init(const char *broker_url, const char *device_name) {
    snprintf(s_topic_moisture, sizeof(s_topic_moisture),
             "onnion/%s/moisture", device_name);
    snprintf(s_topic_motor, sizeof(s_topic_motor),
             "onnion/%s/motor", device_name);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_url,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Client init failed");
        return false;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "Connecting to %s", broker_url);
    return true;
}

void mqtt_manager_publish_moisture(float moisture_pct) {
    if (!s_connected) return;
    char payload[16];
    snprintf(payload, sizeof(payload), "%.1f", moisture_pct);
    esp_mqtt_client_publish(s_client, s_topic_moisture, payload, 0, 1, 0);
}

void mqtt_manager_publish_motor_state(bool is_on) {
    if (!s_connected) return;
    esp_mqtt_client_publish(s_client, s_topic_motor,
                            is_on ? "ON" : "OFF", 0, 1, 0);
}

bool mqtt_manager_is_connected(void) {
    return s_connected;
}
