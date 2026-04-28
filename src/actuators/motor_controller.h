#pragma once

#include <stdbool.h>

/* Water pump / motor connected to GPIO 4.
   Manual mode: user controls the motor via the web page.
   Auto mode:   the PID loop drives motor_set(). */

void motor_controller_init(void);

/* Called by PID loop when not in manual mode. */
void motor_set(bool on);

/* Called by web server callback to enter manual mode. */
void motor_set_manual(bool on);

/* Called by web server callback to return to PID (auto) mode. */
void motor_clear_manual(void);

bool motor_is_on(void);
bool motor_is_manual(void);
