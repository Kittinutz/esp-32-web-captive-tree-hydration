#pragma once

#include <stdbool.h>

/* Called from the HTTP handler task when the user toggles the motor.
   manual=true  → user clicked ON/OFF button; on = desired state.
   manual=false → user clicked "Return to Auto". */
typedef void (*motor_ctrl_cb_t)(bool on, bool manual);

void web_server_start_setup(void);
void web_server_start_control(const char *device_name, motor_ctrl_cb_t motor_cb);
void web_server_stop(void);

/* Called by the main loop each sensor cycle to refresh the displayed status. */
void web_server_update_status(float moisture_pct, bool motor_on, bool motor_manual);
