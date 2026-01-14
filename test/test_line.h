#ifndef TEST_LINE_H
#define TEST_LINE_H

#include "test.h"
#include <string.h>
#include "line.h"
#include <stdlib.h>

#define TEST_BUF_SIZE (READ_BUF_SIZE + 1)

// Callback capture variables
static char captured_lines[10][TEST_BUF_SIZE];
static size_t captured_lengths[10];
static int callback_count;
static int current_capture_index;
static const int MAX_CAPTURES = 10;

// Test callback implementation
void test_line_callback(const buffer_t *line_buf)
{
    if (callback_count < MAX_CAPTURES && line_buf->size < TEST_BUF_SIZE)
    {
        memcpy(captured_lines[callback_count], line_buf->data, line_buf->size);
        captured_lines[callback_count][line_buf->size] = '\0'; // Safe null termination
        captured_lengths[callback_count] = line_buf->size;
        callback_count++;
    }
}

void reset_callback_capture()
{
    callback_count = 0;
    memset(captured_lines, 0, sizeof(captured_lines));
    memset(captured_lengths, 0, sizeof(captured_lengths));
}

void process_string(line_reader_t *lr, const char *str)
{
    for (size_t i = 0; str[i]; i++)
    {
        line_reader_process(lr, str[i]);
    }
}

void test_lr_simple_line()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    process_string(&lr, "hello world\n");

    assert_equal_int(callback_count, 1, "single line callback count");
    assert_equal_int(captured_lengths[0], 11, "single line length");
    assert_string(captured_lines[0], "hello world", "single line content");
}

void test_lr_crlf_handling()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    process_string(&lr, "line1\r\n");

    assert_equal_int(callback_count, 1, "crlf callback count");
    assert_equal_int(captured_lengths[0], 5, "crlf length");
    assert_string(captured_lines[0], "line1", "crlf content");
}

void test_lr_empty_lines_ignored()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    process_string(&lr, "\n\nhello\n\n");

    assert_equal_int(callback_count, 1, "empty lines ignored count");
    assert_equal_int(captured_lengths[0], 5, "empty lines length");
    assert_string(captured_lines[0], "hello", "empty lines content");
}

void test_lr_embedded_cr()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    process_string(&lr, "hello\rworld\n");

    assert_equal_int(callback_count, 1, "embedded cr callback count");
    assert_equal_int(captured_lengths[0], 11, "embedded cr length");
    assert_string(captured_lines[0], "hello\rworld", "embedded cr content");
}

void test_lr_multiple_lines()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    process_string(&lr, "line1\nline2\n");

    assert_equal_int(callback_count, 2, "multiple lines count");
    assert_equal_int(captured_lengths[0], 5, "multiple first length");
    assert_string(captured_lines[0], "line1", "multiple first content");
    assert_equal_int(captured_lengths[1], 5, "multiple second length");
    assert_string(captured_lines[1], "line2", "multiple second content");
}

void test_lr_binary_data()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    // Process chars manually since 'a\0b\n' would stop at null in process_string
    line_reader_process(&lr, 'a');
    line_reader_process(&lr, '\0');
    line_reader_process(&lr, 'b');
    line_reader_process(&lr, '\n');

    assert_equal_int(callback_count, 1, "binary data callback count");
    assert_equal_int(captured_lengths[0], 3, "binary data length");
    assert_equal_int(captured_lines[0][0], 'a', "binary data char 0");
    assert_equal_int(captured_lines[0][1], '\0', "binary data char 1 (null)");
    assert_equal_int(captured_lines[0][2], 'b', "binary data char 2");
}

void test_lr_line_too_long()
{
    line_reader_t lr;
    line_reader_init(&lr, test_line_callback);
    reset_callback_capture();

    // Feed 2049 'a's followed by \n (exceeds READ_BUF_SIZE)
    for (size_t i = 0; i < READ_BUF_SIZE + 1; i++)
        line_reader_process(&lr, 'a');
    line_reader_process(&lr, '\n');

    assert_equal_int(callback_count, 0, "long line ignored");

    // Now process a valid line
    process_string(&lr, "valid\n");

    assert_equal_int(callback_count, 1, "valid line after");
    assert_equal_int(captured_lengths[0], 5, "valid line length");
    assert_string(captured_lines[0], "valid", "valid line content");
}

#endif
