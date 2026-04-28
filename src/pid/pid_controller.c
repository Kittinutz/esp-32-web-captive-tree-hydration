#include "pid/pid_controller.h"

#include "esp_timer.h"

static float clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void pid_init(pid_controller_t *pid,
              float kp, float ki, float kd,
              float setpoint,
              float out_min, float out_max) {
    pid->kp          = kp;
    pid->ki          = ki;
    pid->kd          = kd;
    pid->setpoint    = setpoint;
    pid->out_min     = out_min;
    pid->out_max     = out_max;
    pid->integral    = 0.0f;
    pid->prev_error  = 0.0f;
    pid->prev_time_us = esp_timer_get_time();
}

float pid_compute(pid_controller_t *pid, float measured) {
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - pid->prev_time_us) / 1e6f;
    pid->prev_time_us = now;

    if (dt <= 0.0f) dt = 0.001f;

    float error      = pid->setpoint - measured;
    pid->integral   += error * dt;
    float derivative = (error - pid->prev_error) / dt;
    pid->prev_error  = error;

    /* Anti-windup: clamp integral contribution */
    float max_i = (pid->out_max - pid->out_min) / (pid->ki > 0.0f ? pid->ki : 1.0f);
    pid->integral = clamp(pid->integral, -max_i, max_i);

    float output = pid->kp * error
                 + pid->ki * pid->integral
                 + pid->kd * derivative;

    return clamp(output, pid->out_min, pid->out_max);
}

void pid_set_setpoint(pid_controller_t *pid, float setpoint) {
    pid->setpoint = setpoint;
}

void pid_reset(pid_controller_t *pid) {
    pid->integral    = 0.0f;
    pid->prev_error  = 0.0f;
    pid->prev_time_us = esp_timer_get_time();
}
