#pragma once

#include <stdbool.h>

bool mqtt_manager_init(const char *broker_url, const char *device_name);
void mqtt_manager_publish_moisture(float moisture_pct);
void mqtt_manager_publish_motor_state(bool is_on);
bool mqtt_manager_is_connected(void);
