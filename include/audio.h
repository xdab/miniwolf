#pragma once

#include "buffer.h"
#include <stdbool.h>

typedef int input_callback_t(float_buffer_t *buf);

// Lifecycle
int aud_initialize();
void aud_terminate();

// Configuration
int aud_configure(const char *device_name, int sample_rate, bool do_input, bool do_output);
int aud_start();

// Streaming
void aud_output(const float_buffer_t *buf);

// Audio processing
int aud_process_capture(input_callback_t *callback, float_buffer_t *buf);
void aud_process_playback(void);

// Audio processing of single ALSA period for interleaved TX/RX
bool aud_process_capture_period(input_callback_t *callback, float_buffer_t *buf);
bool aud_process_playback_period(void);

// Get poll descriptor for integration with poll/epoll/select
int aud_get_poll_fd(void);
