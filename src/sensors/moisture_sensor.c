#include "sensors/moisture_sensor.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define TAG "moisture"

/* CM701 capacitive soil moisture sensor – analog output, 3.3 V supply.
   Typical output range (12-bit ADC, Vref = 3.3 V):
     Dry (in air)      : ~2600 – 2800  (~2.1 – 2.3 V)
     Wet (in water)    : ~1100 – 1400  (~0.9 – 1.1 V)
   Run moisture_sensor_read_raw() in both states and replace the
   two constants below with your measured values before deploying. */
#define MOISTURE_RAW_DRY  2700   /* raw ADC when sensor is in open air   */
#define MOISTURE_RAW_WET  1200   /* raw ADC when sensor is fully in water */

/* 30-pin ESP32: A0 = GPIO 36 (VP, input-only) → ADC1 CH0.
   ADC1 must be used — ADC2 is unavailable while WiFi runs. */
#define MOISTURE_ADC_UNIT    ADC_UNIT_1
#define MOISTURE_ADC_CHANNEL ADC_CHANNEL_0   /* A0 = GPIO 36 */

/* Average this many samples per reading to reduce capacitive noise. */
#define SAMPLE_COUNT  16

static adc_oneshot_unit_handle_t s_adc_handle = NULL;

void moisture_sensor_init(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = MOISTURE_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_11,   /* 0 – 3.3 V input range */
        .bitwidth = ADC_BITWIDTH_12,   /* 0 – 4095 */
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle,
                                               MOISTURE_ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "CM701 moisture sensor ready (A0 / GPIO36 / ADC1-CH0)");
}

float moisture_sensor_read_pct(void) {
    int sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc_handle, MOISTURE_ADC_CHANNEL, &raw);
        sum += raw;
    }
    int avg = sum / SAMPLE_COUNT;

    /* CM701 output is inversely proportional to moisture:
       high raw  = dry soil (low capacitance → high V)
       low  raw  = wet soil (high capacitance → low V)          */
    float pct = (float)(MOISTURE_RAW_DRY - avg) /
                (float)(MOISTURE_RAW_DRY - MOISTURE_RAW_WET) * 100.0f;

    if (pct <   0.0f) pct =   0.0f;
    if (pct > 100.0f) pct = 100.0f;

    return pct;
}
