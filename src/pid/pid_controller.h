#pragma once

#include <stdint.h>

typedef struct {
    float   kp, ki, kd;
    float   setpoint;
    float   out_min, out_max;
    /* internal state */
    float   integral;
    float   prev_error;
    int64_t prev_time_us;
} pid_controller_t;

void  pid_init(pid_controller_t *pid,
               float kp, float ki, float kd,
               float setpoint,
               float out_min, float out_max);

float pid_compute(pid_controller_t *pid, float measured);
void  pid_set_setpoint(pid_controller_t *pid, float setpoint);
void  pid_reset(pid_controller_t *pid);
