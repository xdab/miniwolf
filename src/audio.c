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

static snd_pcm_t *pcm_capture = NULL;
static snd_pcm_t *pcm_playback = NULL;
static ring_buffer_t *output_ring = NULL;
static int configured_rate = 0;
static int period_size = ALSA_PERIOD_SIZE;

static int aud_pcm_setup(snd_pcm_t *pcm, int rate, int period_frames)
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

static int aud_pcm_recover(snd_pcm_t *pcm, int err)
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
        return 0;
    }
    return err;
}

static snd_pcm_sframes_t safe_pcm_read(snd_pcm_t *pcm, void *buf, size_t frames)
{
    snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf, frames);
    if (n < 0)
    {
        n = aud_pcm_recover(pcm, n);
        if (n < 0)
        {
            LOG("read error: %s", snd_strerror(n));
            return -1;
        }
        n = snd_pcm_readi(pcm, buf, frames);
        if (n < 0)
        {
            LOG("read error after recovery: %s", snd_strerror(n));
            return -1;
        }
    }
    return n;
}

static snd_pcm_sframes_t safe_pcm_write(snd_pcm_t *pcm, const void *buf, size_t frames)
{
    snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, frames);
    if (n < 0)
    {
        n = aud_pcm_recover(pcm, n);
        if (n < 0)
        {
            LOG("write error: %s", snd_strerror(n));
            return -1;
        }
        n = snd_pcm_writei(pcm, buf, frames);
        if (n < 0)
        {
            LOG("write error after recovery: %s", snd_strerror(n));
            return -1;
        }
    }
    return n;
}

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
            goto fail_capture_open;

        err = aud_pcm_setup(pcm_capture, sample_rate, period_size);
        if (err < 0)
            goto fail_capture_setup;
    }

    if (do_output)
    {
        err = snd_pcm_open(&pcm_playback, device_name, SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0)
            goto fail_playback_open;

        err = aud_pcm_setup(pcm_playback, sample_rate, period_size);
        if (err < 0)
            goto fail_playback_setup;
    }

    return 0;

fail_playback_setup:
    snd_pcm_close(pcm_playback);
    pcm_playback = NULL;
fail_playback_open:
    LOG("failed to configure playback: %s", snd_strerror(err));
fail_capture_setup:
    if (pcm_capture)
    {
        snd_pcm_close(pcm_capture);
        pcm_capture = NULL;
    }
fail_capture_open:
    if (pcm_capture == NULL && pcm_playback == NULL)
        LOG("failed to open capture device '%s': %s", device_name, snd_strerror(err));
    return -1;
}

void aud_output(const float_buffer_t *buf)
{
    assert_buffer_valid(buf);

    ring_write(output_ring, buf->data, buf->size);
}

void aud_input(input_callback_t *callback, float_buffer_t *buf)
{
    if (!callback)
        return;

    assert_buffer_valid(buf);

    if (pcm_capture)
    {
        snd_pcm_sframes_t frames_read = safe_pcm_read(pcm_capture, buf->data, buf->capacity);
        if (frames_read < 0)
            return;

        buf->size = frames_read;
        callback(buf);
    }

    if (pcm_playback)
    {
        unsigned long available = ring_available(output_ring);
        if (available > 0)
        {
            static float output_buffer[ALSA_PERIOD_SIZE];
            unsigned long to_write = available < ALSA_PERIOD_SIZE ? available : ALSA_PERIOD_SIZE;
            unsigned long actually_read = ring_read(output_ring, output_buffer, to_write);

            snd_pcm_sframes_t frames_written = safe_pcm_write(pcm_playback, output_buffer, actually_read);
            if (frames_written < 0)
                return;
        }
    }
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

int aud_get_capture_poll_fds(struct pollfd *pfds, int max_fds)
{
    if (!pcm_capture)
    {
        LOGD("no capture device configured");
        return 0;
    }

    int count = snd_pcm_poll_descriptors_count(pcm_capture);
    if (count > max_fds)
    {
        LOG("not enough space for capture poll fds: need %d, have %d", count, max_fds);
        return -1;
    }

    int actual = snd_pcm_poll_descriptors(pcm_capture, pfds, max_fds);
    if (actual < 0)
    {
        LOG("failed to get capture poll descriptors: %s", snd_strerror(actual));
        return -1;
    }

    LOGD("got %d capture poll descriptors", actual);
    return actual;
}

static int process_single_capture_period(input_callback_t *callback, float_buffer_t *buf)
{
    snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm_capture);
    if (avail < 0)
    {
        avail = aud_pcm_recover(pcm_capture, avail);
        if (avail < 0)
        {
            LOG("avail error: %s", snd_strerror(avail));
            return -1;
        }
        return 1;
    }

    if (avail < period_size)
        return 1;

    snd_pcm_sframes_t frames_read = safe_pcm_read(pcm_capture, buf->data, buf->capacity);
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

int aud_process_capture_events(struct pollfd *pfds, int nfds, input_callback_t *callback, float_buffer_t *buf)
{
    if (!pcm_capture || !callback)
    {
        LOGD("capture disabled or no callback");
        return 0;
    }

    assert_buffer_valid(buf);

    unsigned short revents;
    int err = snd_pcm_poll_descriptors_revents(pcm_capture, pfds, nfds, &revents);
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

        while ((result = process_single_capture_period(callback, buf)) == 0)
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

static int process_single_playback_period(void)
{
    static float output_buffer[ALSA_PERIOD_SIZE];

    unsigned long available = ring_available(output_ring);
    if (available == 0)
        return 1;

    unsigned long to_write = available < ALSA_PERIOD_SIZE ? available : ALSA_PERIOD_SIZE;
    unsigned long actually_read = ring_read(output_ring, output_buffer, to_write);

    if (actually_read == 0)
        return 1;

    snd_pcm_sframes_t frames_written = safe_pcm_write(pcm_playback, output_buffer, actually_read);
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

    while ((result = process_single_playback_period()) == 0)
        periods_written++;

    if (periods_written > 1)
        LOGD("playback: wrote %d periods in single call", periods_written);
}
