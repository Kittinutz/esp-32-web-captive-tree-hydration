#pragma once

/* CM701 capacitive soil moisture sensor.
   Pin: A0 = GPIO 36 → ADC1 CH0 (30-pin ESP32, input-only, ADC1 safe with WiFi).
   Calibrate MOISTURE_RAW_DRY / MOISTURE_RAW_WET in moisture_sensor.c
   by measuring raw ADC in open air and fully submerged water. */

void  moisture_sensor_init(void);
float moisture_sensor_read_pct(void);   /* returns 0.0 – 100.0 */
