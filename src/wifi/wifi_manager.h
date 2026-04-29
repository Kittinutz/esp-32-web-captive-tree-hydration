#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WIFI_SCAN_MAX 20   /* max AP entries returned by wifi_scan_ap() */

void     wifi_manager_init(void);
void     wifi_start_ap(const char *ssid, const char *password);
bool     wifi_start_sta(const char *ssid, const char *password, int timeout_sec);
bool     wifi_is_connected(void);

/* Scan for nearby networks (setup mode only).
   ssids : caller-allocated array of WIFI_SCAN_MAX strings, each 33 bytes.
   count : filled with number of unique SSIDs found.           */
esp_err_t wifi_scan_ap(char ssids[][33], uint16_t *count, uint16_t max_count);
