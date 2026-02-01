#include "audio.h"
#include "ring.h"
#include "common.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#define ALSA_PERIOD_SIZE 4096
#define RING_BUFFER_SIZE 131072
#define MAX_ALSA_FDS 8

static snd_pcm_t *pcm_capture = NULL;
static snd_pcm_t *pcm_playback = NULL;
static ring_buffer_t *output_ring = NULL;
static int configured_rate = 0;
static int period_size = ALSA_PERIOD_SIZE;
static struct pollfd capture_pfds[MAX_ALSA_FDS];
static int capture_pfd_count = 0;

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

    return (pcm == pcm_capture) ? aud_capture_restart() : 0;
}

static snd_pcm_sframes_t aud_pcm_read(snd_pcm_t *pcm, void *buf, snd_pcm_uframes_t frames)
{
    snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf, frames);
    if (n < 0)
    {
        n = aud_stream_recover(pcm, n);
        if (n < 0)
            return -1;
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
    ring_error_t err = ring_init(&output_ring, RING_BUFFER_SIZE);
    if (err != RING_SUCCESS)
    {
        LOG("ring output initialization failed: code %d", err);
        return 1;
    }
    return 0;
}

void aud_terminate(void)
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
    configured_rate = sample_rate;

    if (do_input)
    {
        if (aud_pcm_open(&pcm_capture, device_name, SND_PCM_STREAM_CAPTURE) < 0)
            return -1;
        if (aud_hw_params_apply(pcm_capture, sample_rate, period_size) < 0)
        {
            snd_pcm_close(pcm_capture);
            pcm_capture = NULL;
            return -1;
        }
    }

    if (do_output)
    {
        if (aud_pcm_open(&pcm_playback, device_name, SND_PCM_STREAM_PLAYBACK) < 0)
            goto fail;
        if (aud_hw_params_apply(pcm_playback, sample_rate, period_size) < 0)
        {
            snd_pcm_close(pcm_playback);
            pcm_playback = NULL;
            goto fail;
        }
    }

    return 0;

fail:
    if (pcm_capture)
    {
        snd_pcm_close(pcm_capture);
        pcm_capture = NULL;
    }
    return -1;
}

static int aud_start_fds_setup(void)
{
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
    return 0;
}

int aud_start(void)
{
    if (pcm_capture)
    {
        if (snd_pcm_prepare(pcm_capture) < 0 ||
            snd_pcm_start(pcm_capture) < 0 ||
            aud_start_fds_setup() < 0)
        {
            LOG("failed to start capture stream");
            return -1;
        }
        LOGV("capture stream started");
    }

    if (pcm_playback && snd_pcm_prepare(pcm_playback) < 0)
    {
        LOG("failed to prepare playback stream");
        return -1;
    }

    return 0;
}

void aud_output(const float_buffer_t *buf)
{
    assert_buffer_valid(buf);
    ring_write(output_ring, buf->data, buf->size);
}

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
    }

    if (avail < period_size)
        return 1;

    snd_pcm_sframes_t frames_read = aud_pcm_read(pcm_capture, buf->data, buf->capacity);
    if (frames_read < 0)
        return -1;
    if (frames_read > (snd_pcm_sframes_t)buf->capacity)
        return -1;
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
    if (snd_pcm_poll_descriptors_revents(pcm_capture, capture_pfds, capture_pfd_count, &revents) < 0)
        return -1;

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

    if (aud_pcm_write(pcm_playback, output_buffer, actually_read) < 0)
        return -1;

    return 0;
}

void aud_process_playback(void)
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
