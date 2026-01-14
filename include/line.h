#pragma once

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include "buffer.h"

#define READ_BUF_SIZE 2048

typedef void line_callback_t(const buffer_t *line_buf);

typedef struct line_reader
{
    char buf[READ_BUF_SIZE];
    size_t buf_pos;
    line_callback_t *line_callback;
    bool invalid;

} line_reader_t;

void line_reader_init(line_reader_t *lr, line_callback_t *line_callback);

void line_reader_process(line_reader_t *lr, char ch);
