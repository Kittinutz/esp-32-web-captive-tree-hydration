#include "config/config_manager.h"
#include "wifi/wifi_manager.h"
#include "web/web_server.h"
#include "web/captive_portal.h"
#include "mqtt/mqtt_manager.h"
#include "pid/pid_controller.h"
#include "sensors/moisture_sensor.h"
#include "actuators/motor_controller.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG                  "main"
#define SENSOR_INTERVAL_MS   5000   /* read + publish every 5 seconds */
#define WIFI_TIMEOUT_SEC     30

/* PID output > 0 means moisture is below setpoint → water needed. */
#define PID_MOTOR_THRESHOLD  0.0f

/* ---------- motor web-callback (runs in httpd task) ---------- */

static void on_motor_web_request(bool on, bool manual) {
    if (manual) {
        motor_set_manual(on);
    } else {
        motor_clear_manual();
    }
}

/* ---------- entry point ---------- */

void app_main(void) {
    /* NVS must be initialised before any config read/write. */
    config_init();

    device_config_t cfg = {0};
    bool configured = config_load(&cfg) && config_is_valid(&cfg);

    wifi_manager_init();

    /* ── SETUP MODE ─────────────────────────────────────────────────────── */
    if (!configured) {
        ESP_LOGI(TAG, "No valid config – starting setup mode");
        wifi_start_ap("Onnion-Setup", NULL);   /* open AP, no password */
        captive_portal_dns_start();            /* hijack DNS → auto popup */
        web_server_start_setup();
        ESP_LOGI(TAG, "Connect phone to 'Onnion-Setup' WiFi – setup page pops up automatically");

        /* Block here; the web handler will save config and call esp_restart(). */
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* ── RUNNING MODE ───────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "Config found – connecting to WiFi: %s", cfg.wifi_ssid);

    if (!wifi_start_sta(cfg.wifi_ssid, cfg.wifi_password, WIFI_TIMEOUT_SEC)) {
        ESP_LOGE(TAG, "WiFi failed – clearing config and restarting");
        config_erase();
        esp_restart();
    }

    moisture_sensor_init();
    motor_controller_init();

    pid_controller_t pid;
    pid_init(&pid,
             cfg.pid_kp, cfg.pid_ki, cfg.pid_kd,
             cfg.pid_setpoint,
             0.0f, 100.0f);

    mqtt_manager_init(cfg.mqtt_url, cfg.device_name);
    web_server_start_control(cfg.device_name, on_motor_web_request);

    ESP_LOGI(TAG, "Running: device=%s  setpoint=%.1f%%",
             cfg.device_name, cfg.pid_setpoint);

    /* ── MAIN SENSOR LOOP ───────────────────────────────────────────────── */
    while (1) {
        float moisture   = moisture_sensor_read_pct();
        float pid_output = pid_compute(&pid, moisture);

        /* Auto mode: PID drives the motor.
           Manual mode: user's last web command stays in effect. */
        if (!motor_is_manual()) {
            motor_set(pid_output > PID_MOTOR_THRESHOLD);
        }

        bool motor_on  = motor_is_on();
        bool motor_man = motor_is_manual();

        web_server_update_status(moisture, motor_on, motor_man);
        mqtt_manager_publish_moisture(moisture);
        mqtt_manager_publish_motor_state(motor_on);

        ESP_LOGI(TAG, "Moisture=%.1f%%  PID=%.1f  Motor=%s  Mode=%s",
                 moisture, pid_output,
                 motor_on  ? "ON"  : "OFF",
                 motor_man ? "Manual" : "Auto");

        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}
