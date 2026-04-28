#pragma once

#include <stdbool.h>

void wifi_manager_init(void);
void wifi_start_ap(const char *ssid, const char *password);
bool wifi_start_sta(const char *ssid, const char *password, int timeout_sec);
bool wifi_is_connected(void);
