#include "audio.h"
#include "ring.h"
#include "common.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#define ALSA_PERIOD_SIZE 4096
#define RING_BUFFER_SIZE 131072

static snd_pcm_t *g_pcm_capture = NULL;
static snd_pcm_t *g_pcm_playback = NULL;
static ring_buffer_t *g_output_ring = NULL;

static int aud_hw_params_apply(snd_pcm_t *pcm, int rate, int period_frames)
{
    snd_pcm_hw_params_t *hw_params = NULL;
    int err = snd_pcm_hw_params_malloc(&hw_params);
    if (err < 0)
        return -1;

    err = snd_pcm_hw_params_any(pcm, hw_params) ||
          snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) ||
          snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_FLOAT) ||
          snd_pcm_hw_params_set_channels(pcm, hw_params, 1);
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
    snd_pcm_hw_params_free(hw_params);
    return -1;
}

static int aud_capture_restart(void)
{
    int err = snd_pcm_start(g_pcm_capture);
    if (err < 0)
    {
        LOG("failed to start capture after recovery: %s", snd_strerror(err));
    }
    else
    {
        LOGD("capture restarted successfully");
    }
    return err;
}

static int aud_stream_recover(snd_pcm_t *pcm, int err)
{
    if (err == -EPIPE)
    {
        LOGD("xrun on %s, recovering", pcm == g_pcm_capture ? "capture" : "playback");
        err = snd_pcm_prepare(pcm);
    }
    else if (err == -ESTRPIPE)
    {
        LOGV("stream suspended, waiting for resume");
        while ((err = snd_pcm_resume(pcm)) == -EAGAIN)
            usleep(100000);
        if (err < 0)
            err = snd_pcm_prepare(pcm);
    }

    if (err < 0)
    {
        LOG("failed to recover: %s", snd_strerror(err));
        return err;
    }

    int restart_err = (pcm == g_pcm_capture) ? aud_capture_restart() : 0;
    if (restart_err < 0)
    {
        LOGD("stream recovery failed for %s", pcm == g_pcm_capture ? "capture" : "playback");
    }
    else
    {
        LOGD("stream recovery succeeded for %s", pcm == g_pcm_capture ? "capture" : "playback");
    }
    return restart_err;
}

static snd_pcm_sframes_t aud_pcm_read(snd_pcm_t *pcm, void *buf, snd_pcm_uframes_t frames)
{
    snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf, frames);
    if (n < 0)
    {
        LOGD("read error: %s, attempting recovery", snd_strerror(n));
        n = aud_stream_recover(pcm, n);
        if (n < 0)
        {
            LOGD("read recovery failed");
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

static snd_pcm_sframes_t aud_pcm_write(snd_pcm_t *pcm, const void *buf, snd_pcm_uframes_t frames)
{
    snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, frames);
    if (n < 0)
    {
        n = aud_stream_recover(pcm, n);
        if (n < 0)
            return -1;
        n = snd_pcm_writei(pcm, buf, frames);
        if (n < 0)
        {
            LOG("write error after recovery: %s", snd_strerror(n));
            return -1;
        }
    }
    return n;
}

int aud_initialize(void)
{
    ring_error_t err = ring_init(&g_output_ring, RING_BUFFER_SIZE);
    if (err != RING_SUCCESS)
    {
        LOG("ring output initialization failed: code %d", err);
        return 1;
    }
    return 0;
}

void aud_terminate(void)
{
    if (g_pcm_capture)
    {
        snd_pcm_close(g_pcm_capture);
        g_pcm_capture = NULL;
    }
    if (g_pcm_playback)
    {
        snd_pcm_close(g_pcm_playback);
        g_pcm_playback = NULL;
    }
    ring_destroy(g_output_ring);
    g_output_ring = NULL;
}

static int aud_pcm_open(snd_pcm_t **pcm, const char *device_name, snd_pcm_stream_t stream)
{
    int err = snd_pcm_open(pcm, device_name, stream, 0);
    if (err < 0)
    {
        LOG("failed to open %s device '%s': %s",
            stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback",
            device_name, snd_strerror(err));
        return -1;
    }
    return 0;
}

int aud_configure(const char *device_name, int sample_rate, bool do_input, bool do_output)
{
    if (!do_input && !do_output)
        return 0;

    nonnull(device_name, "device_name");

    if (do_input)
    {
        if (aud_pcm_open(&g_pcm_capture, device_name, SND_PCM_STREAM_CAPTURE) < 0)
            return -1;
        if (aud_hw_params_apply(g_pcm_capture, sample_rate, ALSA_PERIOD_SIZE) < 0)
        {
            snd_pcm_close(g_pcm_capture);
            g_pcm_capture = NULL;
            return -1;
        }
    }

    if (do_output)
    {
        if (aud_pcm_open(&g_pcm_playback, device_name, SND_PCM_STREAM_PLAYBACK) < 0)
            goto fail;
        if (aud_hw_params_apply(g_pcm_playback, sample_rate, ALSA_PERIOD_SIZE) < 0)
        {
            snd_pcm_close(g_pcm_playback);
            g_pcm_playback = NULL;
            goto fail;
        }
    }

    return 0;

fail:
    if (g_pcm_capture)
    {
        snd_pcm_close(g_pcm_capture);
        g_pcm_capture = NULL;
    }
    return -1;
}

int aud_start(void)
{
    if (g_pcm_capture)
    {
        if (snd_pcm_prepare(g_pcm_capture) < 0 ||
            snd_pcm_start(g_pcm_capture) < 0)
        {
            LOG("failed to start capture stream");
            return -1;
        }
        LOGV("capture stream started");
    }

    if (g_pcm_playback && snd_pcm_prepare(g_pcm_playback) < 0)
    {
        LOG("failed to prepare playback stream");
        return -1;
    }

    return 0;
}

void aud_output(const float_buffer_t *buf)
{
    assert_buffer_valid(buf);
    ring_write(g_output_ring, buf->data, buf->size);
}

bool aud_process_capture_period(input_callback_t *callback, float_buffer_t *buf)
{
    if (!g_pcm_capture || !callback)
        return false;

    assert_buffer_valid(buf);

    snd_pcm_sframes_t avail = snd_pcm_avail_update(g_pcm_capture);
    if (avail < 0)
    {
        LOGD("avail error: %s", snd_strerror(avail));
        avail = aud_stream_recover(g_pcm_capture, avail);
        if (avail < 0)
        {
            LOG("avail error: %s", snd_strerror(avail));
            return false;
        }
        avail = snd_pcm_avail_update(g_pcm_capture);
        if (avail < 0)
        {
            LOG("avail error after recovery: %s", snd_strerror(avail));
            return false;
        }
    }

    if (avail == 0)
        return false;

    snd_pcm_sframes_t to_read = avail < buf->capacity ? avail : buf->capacity;
    snd_pcm_sframes_t frames_read = aud_pcm_read(g_pcm_capture, buf->data, to_read);
    if (frames_read < 0)
    {
        LOGD("read error in capture");
        return false;
    }
    if (frames_read == 0)
        return false;

    buf->size = frames_read;
    callback(buf);
    return true;
}

int aud_process_capture(input_callback_t *callback, float_buffer_t *buf)
{
    int periods_processed = 0;
    while (aud_process_capture_period(callback, buf))
        periods_processed++;
    return periods_processed > 0 ? 0 : -1;
}

static int aud_playback_write_period_internal(void)
{
    static float output_buffer[ALSA_PERIOD_SIZE];

    unsigned long available = ring_available(g_output_ring);
    if (available == 0)
        return 1;

    unsigned long to_write = available < ALSA_PERIOD_SIZE ? available : ALSA_PERIOD_SIZE;
    unsigned long actually_read = ring_read(g_output_ring, output_buffer, to_write);
    if (actually_read == 0)
        return 1;

    if (aud_pcm_write(g_pcm_playback, output_buffer, actually_read) < 0)
        return -1;

    return 0;
}

bool aud_process_playback_period(void)
{
    if (!g_pcm_playback)
        return false;

    if (ring_available(g_output_ring) == 0)
        return false;

    int result = aud_playback_write_period_internal();
    return result == 0;
}

void aud_process_playback(void)
{
    if (!g_pcm_playback)
        return;

    unsigned long ring_avail = ring_available(g_output_ring);
    if (ring_avail == 0)
        return;

    int periods_written = 0;
    while (aud_process_playback_period())
        periods_written++;

    if (periods_written == 0)
    {
        LOGD("playback: no periods written (ring_avail=%lu)", ring_avail);
    }
    else if (periods_written > 1)
    {
        LOGD("playback: wrote %d periods in single call", periods_written);
    }
}

int aud_get_poll_fd(void)
{
    if (!g_pcm_capture)
        return -1;

    struct pollfd pfd;
    int count = snd_pcm_poll_descriptors(g_pcm_capture, &pfd, 1);
    if (count <= 0)
        return -1;

    return pfd.fd;
}
