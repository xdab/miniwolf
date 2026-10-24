#pragma once

#include "buffer.h"
#include "kiss.h"
#include "miniwolf.h"

void loop_run(miniwolf_t *mw);

// RX/TX hooks wired by src/miniwolf.c and src/main.c
int audio_input_callback(float_buffer_t *buf);
void tnc2_input_callback(const buffer_t *line_buf);
void kiss_input_callback(kiss_message_t *kiss_msg);
void tcp_client_connect_cb(int fd, void *user_data);
void tcp_client_disconnect_cb(int fd, void *user_data);
void uds_client_connect_cb(int fd, void *user_data);
void uds_client_disconnect_cb(int fd, void *user_data);
