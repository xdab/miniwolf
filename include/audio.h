#pragma once

#include "buffer.h"
#include <stdbool.h>

typedef int input_callback_t(float_buffer_t *buf);

// Lifecycle
int aud_initialize();
void aud_terminate();

// Configuration
int aud_configure(const char *device_name, int sample_rate, bool do_input, bool do_output);
void aud_list_devices();
int aud_start();

// Streaming
void aud_output(const float_buffer_t *buf);
void aud_input(input_callback_t *callback, float_buffer_t *buf);

// File descriptor polling integration
int aud_get_capture_fd_count(void);
int aud_get_capture_fd(int index);
int aud_process_capture_ready(int fd, input_callback_t *callback, float_buffer_t *buf);
void aud_process_playback(void);
