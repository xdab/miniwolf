#include "audio.h"
#include "ring.h"
#include "common.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#define ALSA_PERIOD_SIZE 4096
#define RING_BUFFER_SIZE 131072
#define MAX_ALSA_FDS 8

// State
static snd_pcm_t *pcm_capture = NULL;
static snd_pcm_t *pcm_playback = NULL;
static ring_buffer_t *output_ring = NULL;
static int configured_rate = 0;
static int period_size = ALSA_PERIOD_SIZE;
static struct pollfd capture_pfds[MAX_ALSA_FDS];
static int capture_pfd_count = 0;

// ============================================================================
// ALSA Hardware Configuration
// ============================================================================

static int aud_hw_params_apply(snd_pcm_t *pcm, int rate, int period_frames)
{
    snd_pcm_hw_params_t *hw_params = NULL;
    int err;

    err = snd_pcm_hw_params_malloc(&hw_params);
    if (err < 0)
        goto fail_alloc;

    err = snd_pcm_hw_params_any(pcm, hw_params);
    if (err < 0)
        goto fail;

    err = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
        goto fail;

    err = snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_FLOAT);
    if (err < 0)
        goto fail;

    err = snd_pcm_hw_params_set_channels(pcm, hw_params, 1);
    if (err < 0)
        goto fail;

    unsigned int actual_rate = rate;
    err = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &actual_rate, 0);
    if (err < 0)
        goto fail;

    snd_pcm_uframes_t period_frames_val = period_frames;
    err = snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_frames_val, 0);
    if (err < 0)
        goto fail;

    snd_pcm_uframes_t buffer_frames = period_frames * 4;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &buffer_frames);
    if (err < 0)
        goto fail;

    err = snd_pcm_hw_params(pcm, hw_params);
    if (err < 0)
        goto fail;

    snd_pcm_hw_params_free(hw_params);
    return 0;

fail:
    LOG("hw params setup error: %s", snd_strerror(err));
fail_alloc:
    if (hw_params)
        snd_pcm_hw_params_free(hw_params);
    return -1;
}

// ============================================================================
// ALSA Error Recovery
// ============================================================================

static int aud_capture_restart(void)
{
    int err = snd_pcm_start(pcm_capture);
    if (err < 0)
        LOG("failed to start capture after recovery: %s", snd_strerror(err));
    return err;
}

static int aud_stream_recover(snd_pcm_t *pcm, int err)
{
    if (err == -EPIPE)
    {
        LOGV("xrun occurred, recovering");
        err = snd_pcm_prepare(pcm);
        if (err < 0)
        {
            LOG("failed to recover from xrun: %s", snd_strerror(err));
            return err;
        }
        if (pcm == pcm_capture)
            return aud_capture_restart();
        return 0;
    }
    else if (err == -ESTRPIPE)
    {
        LOGV("stream suspended, waiting for resume");
        while ((err = snd_pcm_resume(pcm)) == -EAGAIN)
            usleep(100000);
        if (err < 0)
        {
            err = snd_pcm_prepare(pcm);
            if (err < 0)
            {
                LOG("failed to recover from suspend: %s", snd_strerror(err));
                return err;
            }
        }
        if (pcm == pcm_capture)
            return aud_capture_restart();
        return 0;
    }
    return err;
}

// ============================================================================
// Safe ALSA I/O with Recovery
// ============================================================================

typedef snd_pcm_sframes_t (*pcm_io_func_t)(snd_pcm_t *, void *, snd_pcm_uframes_t);

static snd_pcm_sframes_t aud_io_with_recovery(snd_pcm_t *pcm, void *buf, size_t frames,
                                              pcm_io_func_t io_func, const char *op_name)
{
    snd_pcm_sframes_t n = io_func(pcm, buf, frames);
    if (n < 0)
    {
        n = aud_stream_recover(pcm, n);
        if (n < 0)
        {
            LOG("%s error: %s", op_name, snd_strerror(n));
            return -1;
        }
        n = io_func(pcm, buf, frames);
        if (n < 0)
        {
            LOG("%s error after recovery: %s", op_name, snd_strerror(n));
            return -1;
        }
    }
    return n;
}

static snd_pcm_sframes_t aud_read_frames(snd_pcm_t *pcm, void *buf, size_t frames)
{
    return aud_io_with_recovery(pcm, buf, frames, (pcm_io_func_t)snd_pcm_readi, "read");
}

static snd_pcm_sframes_t aud_write_frames(snd_pcm_t *pcm, const void *buf, size_t frames)
{
    return aud_io_with_recovery(pcm, (void *)buf, frames, (pcm_io_func_t)snd_pcm_writei, "write");
}

// ============================================================================
// Public API: Lifecycle
// ============================================================================

int aud_initialize()
{
    ring_error_t err = ring_init(&output_ring, RING_BUFFER_SIZE);
    if (err != RING_SUCCESS)
    {
        LOG("ring output initialization failed: code %d", err);
        return 1;
    }

    return 0;
}

void aud_terminate()
{
    if (pcm_capture)
    {
        snd_pcm_close(pcm_capture);
        pcm_capture = NULL;
    }
    if (pcm_playback)
    {
        snd_pcm_close(pcm_playback);
        pcm_playback = NULL;
    }
    ring_destroy(output_ring);
    output_ring = NULL;
}

// ============================================================================
// Public API: Device Enumeration
// ============================================================================

void aud_list_devices()
{
    void **hints;
    const char *filter = "pcm";

    if (snd_device_name_hint(-1, filter, &hints) < 0)
    {
        LOG("failed to get device hints");
        return;
    }

    printf("%-5s%-60s\n", "Cap", "Name");

    for (void **n = hints; *n != NULL; n++)
    {
        char *name = snd_device_name_get_hint(*n, "NAME");
        if (!name)
            continue;

        char *descr = snd_device_name_get_hint(*n, "DESC");
        char *io = snd_device_name_get_hint(*n, "IOID");

        char cap[3] = "--";
        if (io == NULL)
        {
            cap[0] = 'I';
            cap[1] = 'O';
        }
        else if (strcmp(io, "Input") == 0)
            cap[0] = 'I';
        else if (strcmp(io, "Output") == 0)
            cap[1] = 'O';

        printf("%-5s%-60s\n", cap, name);

        free(name);
        if (descr)
            free(descr);
        if (io)
            free(io);
    }

    snd_device_name_free_hint(hints);
}

// ============================================================================
// Public API: Configuration
// ============================================================================

int aud_configure(const char *device_name, int sample_rate, bool do_input, bool do_output)
{
    int err;

    if (!do_input && !do_output)
        return 0;

    nonnull(device_name, "device_name");
    configured_rate = sample_rate;

    if (do_input)
    {
        err = snd_pcm_open(&pcm_capture, device_name, SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0)
        {
            LOG("failed to open capture device '%s': %s", device_name, snd_strerror(err));
            return -1;
        }

        err = aud_hw_params_apply(pcm_capture, sample_rate, period_size);
        if (err < 0)
        {
            snd_pcm_close(pcm_capture);
            pcm_capture = NULL;
            return -1;
        }
    }

    if (do_output)
    {
        err = snd_pcm_open(&pcm_playback, device_name, SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0)
        {
            LOG("failed to open playback device '%s': %s", device_name, snd_strerror(err));
            goto fail_cleanup;
        }

        err = aud_hw_params_apply(pcm_playback, sample_rate, period_size);
        if (err < 0)
        {
            snd_pcm_close(pcm_playback);
            pcm_playback = NULL;
            goto fail_cleanup;
        }
    }

    return 0;

fail_cleanup:
    if (pcm_capture)
    {
        snd_pcm_close(pcm_capture);
        pcm_capture = NULL;
    }
    return -1;
}

int aud_start()
{
    int err;

    if (pcm_capture)
    {
        err = snd_pcm_prepare(pcm_capture);
        if (err < 0)
        {
            LOG("failed to prepare capture stream: %s", snd_strerror(err));
            return -1;
        }

        err = snd_pcm_start(pcm_capture);
        if (err < 0)
        {
            LOG("failed to start capture stream: %s", snd_strerror(err));
            return -1;
        }
        LOGV("capture stream started");

        int count = snd_pcm_poll_descriptors_count(pcm_capture);
        if (count > MAX_ALSA_FDS)
        {
            LOG("too many capture poll fds: %d > %d", count, MAX_ALSA_FDS);
            return -1;
        }

        capture_pfd_count = snd_pcm_poll_descriptors(pcm_capture, capture_pfds, MAX_ALSA_FDS);
        if (capture_pfd_count < 0)
        {
            LOG("failed to get capture poll descriptors: %s", snd_strerror(capture_pfd_count));
            capture_pfd_count = 0;
            return -1;
        }
        LOGD("initialized %d capture poll fds for selector", capture_pfd_count);
    }

    if (pcm_playback)
    {
        err = snd_pcm_prepare(pcm_playback);
        if (err < 0)
        {
            LOG("failed to prepare playback stream: %s", snd_strerror(err));
            return -1;
        }
    }

    return 0;
}

// ============================================================================
// Public API: Streaming Output
// ============================================================================

void aud_output(const float_buffer_t *buf)
{
    assert_buffer_valid(buf);

    ring_write(output_ring, buf->data, buf->size);
}

// ============================================================================
// Public API: Poll-based Capture
// ============================================================================

int aud_get_capture_fd_count(void)
{
    return capture_pfd_count;
}

int aud_get_capture_fd(int index)
{
    if (index < 0 || index >= capture_pfd_count)
        return -1;
    return capture_pfds[index].fd;
}

static int aud_capture_process_period(input_callback_t *callback, float_buffer_t *buf)
{
    snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm_capture);
    if (avail < 0)
    {
        avail = aud_stream_recover(pcm_capture, avail);
        if (avail < 0)
        {
            LOG("avail error: %s", snd_strerror(avail));
            return -1;
        }
        avail = snd_pcm_avail_update(pcm_capture);
        if (avail < 0)
        {
            LOG("avail error after recovery: %s", snd_strerror(avail));
            return -1;
        }
        if (avail < period_size)
            return 1;
    }

    if (avail < period_size)
        return 1;

    snd_pcm_sframes_t frames_read = aud_read_frames(pcm_capture, buf->data, buf->capacity);
    if (frames_read < 0)
        return -1;

    if (frames_read > (snd_pcm_sframes_t)buf->capacity)
    {
        LOG("frames_read exceeds buffer capacity: %ld > %lu", frames_read, (unsigned long)buf->capacity);
        return -1;
    }

    if (frames_read <= 0)
        return 1;

    buf->size = frames_read;
    callback(buf);
    return 0;
}

int aud_process_capture_ready(int fd, input_callback_t *callback, float_buffer_t *buf)
{
    if (!pcm_capture || !callback || capture_pfd_count == 0)
        return 0;

    assert_buffer_valid(buf);

    int pfd_index = -1;
    for (int i = 0; i < capture_pfd_count; i++)
    {
        if (capture_pfds[i].fd == fd)
        {
            pfd_index = i;
            break;
        }
    }

    if (pfd_index < 0)
        return -1;

    capture_pfds[pfd_index].revents = POLLIN;

    unsigned short revents;
    int err = snd_pcm_poll_descriptors_revents(pcm_capture, capture_pfds, capture_pfd_count, &revents);
    if (err < 0)
    {
        LOG("failed to get poll revents: %s", snd_strerror(err));
        return -1;
    }

    LOGD("capture revents: 0x%x", revents);

    if (revents & POLLIN)
    {
        int periods_processed = 0;
        int result;

        while ((result = aud_capture_process_period(callback, buf)) == 0)
            periods_processed++;

        if (result == -1)
            return -1;

        if (periods_processed > 1)
            LOGD("drained %d periods in single poll event", periods_processed);
    }

    if (revents & POLLERR)
    {
        LOGV("poll error on capture device");
        snd_pcm_prepare(pcm_capture);
        snd_pcm_start(pcm_capture);
    }

    return 0;
}

// ============================================================================
// Public API: Playback Processing
// ============================================================================

static int aud_playback_write_period(void)
{
    static float output_buffer[ALSA_PERIOD_SIZE];

    unsigned long available = ring_available(output_ring);
    if (available == 0)
        return 1;

    unsigned long to_write = available < ALSA_PERIOD_SIZE ? available : ALSA_PERIOD_SIZE;
    unsigned long actually_read = ring_read(output_ring, output_buffer, to_write);

    if (actually_read == 0)
        return 1;

    snd_pcm_sframes_t frames_written = aud_write_frames(pcm_playback, output_buffer, actually_read);
    if (frames_written < 0)
        return -1;

    return 0;
}

void aud_process_playback()
{
    if (!pcm_playback)
        return;

    int periods_written = 0;
    int result;

    while ((result = aud_playback_write_period()) == 0)
        periods_written++;

    if (periods_written > 1)
        LOGD("playback: wrote %d periods in single call", periods_written);
}
