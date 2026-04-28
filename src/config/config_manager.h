#pragma once

#include <stdbool.h>

#define DEVICE_NAME_MAX_LEN 64
#define WIFI_SSID_MAX_LEN   64
#define WIFI_PASS_MAX_LEN   64
#define MQTT_URL_MAX_LEN    256

typedef struct {
    char  device_name[DEVICE_NAME_MAX_LEN];
    char  wifi_ssid[WIFI_SSID_MAX_LEN];
    char  wifi_password[WIFI_PASS_MAX_LEN];
    char  mqtt_url[MQTT_URL_MAX_LEN];
    float pid_setpoint;
    float pid_kp;
    float pid_ki;
    float pid_kd;
} device_config_t;

void config_init(void);
bool config_load(device_config_t *cfg);
void config_save(const device_config_t *cfg);
void config_erase(void);
bool config_is_valid(const device_config_t *cfg);
