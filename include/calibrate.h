#pragma once

#include "buffer.h"
#include <stdbool.h>

int calibrate_init(int sample_rate, float gain_2200);
int calibrate_audio_callback(float_buffer_t *buf);
void calibrate_run(void);
void calibrate_free(void);
