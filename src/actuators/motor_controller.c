#include "actuators/motor_controller.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define TAG               "motor"
#define GPIO_MOTOR_ENABLE GPIO_NUM_4   /* HIGH = motor enabled, LOW = disabled */

static bool s_on     = false;
static bool s_manual = false;

void motor_controller_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << GPIO_MOTOR_ENABLE),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(GPIO_MOTOR_ENABLE, 0);
    ESP_LOGI(TAG, "Motor controller ready (GPIO%d)", GPIO_MOTOR_ENABLE);
}

static void apply(bool on) {
    s_on = on;
    gpio_set_level(GPIO_MOTOR_ENABLE, on ? 1 : 0);
    ESP_LOGI(TAG, "Motor %s", on ? "ON" : "OFF");
}

void motor_set(bool on) {
    if (!s_manual) apply(on);
}

void motor_set_manual(bool on) {
    s_manual = true;
    apply(on);
    ESP_LOGI(TAG, "Manual mode: %s", on ? "ON" : "OFF");
}

void motor_clear_manual(void) {
    s_manual = false;
    ESP_LOGI(TAG, "Returned to Auto (PID) mode");
}

bool motor_is_on(void) {
    return s_on;
}

bool motor_is_manual(void) {
    return s_manual;
}
