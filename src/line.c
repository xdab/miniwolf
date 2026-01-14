#include "line.h"
#include "common.h"

void line_reader_init(line_reader_t *lr, line_callback_t *line_callback)
{
    nonnull(lr, "lr");
    nonnull(line_callback, "line_callback");

    lr->buf_pos = 0;
    lr->line_callback = line_callback;
    lr->invalid = false;
}

void line_reader_process(line_reader_t *lr, char ch)
{
    nonnull(lr, "lr");

    if (ch == '\n')
    {
        if (lr->buf_pos == 0)
            return; // Blank line
        if (lr->buf_pos <= READ_BUF_SIZE && lr->buf[lr->buf_pos - 1] == '\r')
            lr->buf_pos--; // Handle CRLF delimitation
        if (lr->buf_pos == 0)
            return; // Blank line, but CRLF delimited

        // Convert internal buffer to buffer_t for callback
        buffer_t line_buf = {
            .data = (unsigned char *)lr->buf,
            .capacity = READ_BUF_SIZE,
            .size = lr->buf_pos};

        if (!lr->invalid && lr->line_callback)
            lr->line_callback(&line_buf);

        lr->buf_pos = 0;
        lr->invalid = false;
        return;
    }

    if (!lr->invalid)
    {
        if (lr->buf_pos < READ_BUF_SIZE)
            lr->buf[lr->buf_pos++] = ch;
        else
            lr->invalid = true;
    }
}
