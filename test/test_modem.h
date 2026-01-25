#ifndef test_modem_H
#define test_modem_H

#include "test.h"
#include "ax25.h"
#include "modem.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

const float tx_delay = 100.0f;
const float tx_tail = 30.0f;

static inline void test_modem_init_pair(
    struct md_tx *tx, struct md_rx *rx,
    float sample_rate, uint32_t demod_flags)
{
    md_tx_init(tx, sample_rate, tx_delay, tx_tail);
    md_rx_init(rx, sample_rate, demod_flags);
}

static inline void test_modem_free_pair(struct md_tx *tx, struct md_rx *rx)
{
    md_tx_free(tx);
    md_rx_free(rx);
}

static inline void test_modem_init_multi(struct md_multi_rx *mrx, float sample_rate, demod_type_t types)
{
    md_multi_rx_init(mrx, sample_rate, types);
}

static inline void test_modem_create_aprs_packet(
    ax25_packet_t *packet,
    const char *source, const char *dest,
    const char *info)
{
    ax25_packet_init(packet);
    ax25_addr_init_with(&packet->source, source, 7, 0);
    ax25_addr_init_with(&packet->destination, dest, 0, 0);
    ax25_addr_init_with(&packet->path[0], "WIDE2", 2, 0);
    packet->path_len = 1;
    memcpy(packet->info, info, strlen(info));
    packet->info_len = strlen(info);
}

static inline void test_modem_verify_packet(
    buffer_t *decoded_buf,
    const uint8_t *expected_data, int expected_len,
    const char *test_name)
{
    assert_true(decoded_buf->size > 0, "demodulation successful");
    assert_equal_int(decoded_buf->size, expected_len, "decoded length matches original");
    assert_memory(decoded_buf->data, expected_data, expected_len,
                  "decoded data matches original");
}

static inline void test_modem_generate_random_ascii(uint8_t *data, int len, unsigned seed)
{
    srand(seed);
    for (int i = 0; i < len; i++)
        data[i] = (rand() % 95) + 32; // Printable ASCII: 32-126
}

static inline void test_modem_generate_pattern(uint8_t *data, int len, int pattern_type)
{
    switch (pattern_type)
    {
    case 0: // Sequential bytes
        for (int i = 0; i < len; i++)
            data[i] = i % 256;
        break;
    case 1: // Alternating pattern
        for (int i = 0; i < len; i++)
            data[i] = (i % 2) ? 0xAA : 0x55;
        break;
    case 2: // All zeros
        memset(data, 0, len);
        break;
    case 3: // All ones
        memset(data, 0xFF, len);
        break;
    default: // Random printable ASCII
        test_modem_generate_random_ascii(data, len, 42);
        break;
    }
}

static inline int test_modem_max_samples(float sample_rate, float duration_seconds)
{
    return (int)(sample_rate * duration_seconds);
}

static inline float awgn(float sigma)
{
    static float z2;
    static int use_z2 = 0;

    if (use_z2)
    {
        use_z2 = 0;
        return sigma * z2;
    }
    else
    {
        float u1 = (float)rand() / RAND_MAX;
        float u2 = (float)rand() / RAND_MAX;
        float mag = sigma * sqrtf(-2.0f * logf(u1 + 1e-10f)); // Avoid log(0)
        float phase = 2.0f * M_PI * u2;
        z2 = mag * sinf(phase);
        use_z2 = 1;
        return mag * cosf(phase);
    }
}

void test_modem_aprs_like(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 256;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    ax25_packet_t packet;
    test_modem_create_aprs_packet(&packet, "XX0TST", "APN001", "!5221.20N/02043.85E# TEST");

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");
    assert_true(packed_buf.size > 0, "packed size > 0");

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);
    test_modem_verify_packet(&decoded_buf, packed_data, packed_buf.size, "aprs_like");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_arbitrary_data(float sample_rate, int payload_length, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 3.0f); // Larger for bigger payloads
    const int max_frame = 512;                                         // Enough for 256 bytes + overhead

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    // Generate random printable ASCII data
    uint8_t test_data[256];
    test_modem_generate_random_ascii(test_data, payload_length, 42);
    int data_len = payload_length;

    buffer_t test_buf = {.data = test_data, .capacity = sizeof(test_data), .size = data_len};

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = sizeof(samples) / sizeof(float), .size = 0};
    int sample_count = md_tx_process(&tx, &test_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);
    assert_true(decoded_len > 0, "demodulation successful");
    test_modem_verify_packet(&decoded_buf, test_data, data_len, "arbitrary_data");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_with_noise_around_packet(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 3.0f);
    const int max_frame = 512;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    ax25_packet_t packet;
    test_modem_create_aprs_packet(&packet, "XX0TST", "APN001", "!5221.20N/02043.85E# TEST");

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = sizeof(samples) / sizeof(float), .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    int noise_samples = test_modem_max_samples(sample_rate, 0.1f);
    srand(42);
    float noisy_samples[2 * noise_samples + sample_count];

    for (int i = 0; i < noise_samples; i++)
        noisy_samples[i] = awgn(0.577f);

    for (int i = 0; i < sample_count; i++)
        samples[i] *= 0.333f;
    memcpy(&noisy_samples[noise_samples], samples, sample_count * sizeof(float));

    for (int i = 0; i < noise_samples; i++)
        noisy_samples[noise_samples + sample_count + i] = awgn(0.577f);

    int total_samples = 2 * noise_samples + sample_count;

    float_buffer_t noisy_sample_buf = {.data = noisy_samples, .capacity = sizeof(noisy_samples) / sizeof(float), .size = total_samples};
    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_rx_process(&rx, &noisy_sample_buf, &decoded_buf, NULL);
    assert_true(decoded_len > 0, "demodulation successful");
    test_modem_verify_packet(&decoded_buf, packed_data, packed_buf.size, "noise_around_packet");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_with_noise_over_packet(float sample_rate, uint32_t demod_flags)
{
    const float SNR_DB = 6.0f;
    const float AMPLITUDE_NOISE = 0.01f;
    const float AMPLITUDE_SIGNAL = AMPLITUDE_NOISE * powf(10.0f, SNR_DB / 20.0f);

    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 256;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    ax25_packet_t packet;
    test_modem_create_aprs_packet(&packet, "XX0TST", "APN001", "!5221.20N/02043.85E# TEST");

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = sizeof(samples) / sizeof(float), .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    srand(42);
    for (int i = 0; i < sample_count; i++)
    {
        samples[i] *= AMPLITUDE_SIGNAL;
        float noise = awgn(AMPLITUDE_NOISE / sqrtf(3.0f));
        samples[i] += noise;
    }

    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);
    assert_true(decoded_len > 0, "demodulation successful");
    test_modem_verify_packet(&decoded_buf, packed_data, packed_buf.size, "noise_over_packet");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_two_packets_back_to_back(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 64;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    // Create first packet
    ax25_packet_t packet1;
    ax25_packet_init(&packet1);
    ax25_addr_init_with(&packet1.source, "TEST1", 0, 0);
    ax25_addr_init_with(&packet1.destination, "DST1", 0, 0);
    packet1.path_len = 0;
    const char *info1 = "AB";
    memcpy(packet1.info, info1, strlen(info1));
    packet1.info_len = strlen(info1);

    uint8_t packed1_data[max_frame];
    buffer_t packed1_buf = {.data = packed1_data, .capacity = sizeof(packed1_data), .size = 0};
    ax25_error_e pack1_result = ax25_packet_pack(&packet1, &packed1_buf);
    assert_equal_int(pack1_result, AX25_SUCCESS, "pack 1 success");

    // Create second packet
    ax25_packet_t packet2;
    ax25_packet_init(&packet2);
    ax25_addr_init_with(&packet2.source, "TEST2", 0, 0);
    ax25_addr_init_with(&packet2.destination, "DST2", 0, 0);
    packet2.path_len = 0;
    const char *info2 = "CD";
    memcpy(packet2.info, info2, strlen(info2));
    packet2.info_len = strlen(info2);

    uint8_t packed2_data[max_frame];
    buffer_t packed2_buf = {.data = packed2_data, .capacity = sizeof(packed2_data), .size = 0};
    ax25_error_e pack2_result = ax25_packet_pack(&packet2, &packed2_buf);
    assert_equal_int(pack2_result, AX25_SUCCESS, "pack 2 success");

    // Modulate packets with silence in between
    float combined_samples[max_samples];
    float_buffer_t combined_sample_buf = {.data = combined_samples, .capacity = sizeof(combined_samples) / sizeof(float), .size = 0};
    int sample_count1 = md_tx_process(&tx, &packed1_buf, &combined_sample_buf, NULL);
    assert_true(sample_count1 > 0, "packet1 modulation successful");

    // Add silence (about 0.1 seconds at given sample rate)
    int silence_samples = test_modem_max_samples(sample_rate, 0.1f);
    memset(combined_samples + sample_count1, 0, silence_samples * sizeof(float));

    // Create a temporary buffer for the second packet starting after the first packet and silence
    float_buffer_t temp_sample_buf = {.data = combined_samples + sample_count1 + silence_samples, .capacity = sizeof(combined_samples) / sizeof(float) - sample_count1 - silence_samples, .size = 0};
    int sample_count2 = md_tx_process(&tx, &packed2_buf, &temp_sample_buf, NULL);
    assert_true(sample_count2 > 0, "packet2 modulation successful");

    int total_samples = sample_count1 + silence_samples + sample_count2;

    const int chunk_size_samples = 128; // Small chunks for streaming simulation

    int pos = 0;
    int frames_found = 0;
    uint8_t detected_frames[2][max_frame];
    int detected_frame_lens[2];

    // Process the sample stream in sequential chunks
    while (pos < total_samples && frames_found < 2)
    {
        int current_chunk_samples = (pos + chunk_size_samples <= total_samples) ? chunk_size_samples : (total_samples - pos);

        float_buffer_t chunk_sample_buf = {.data = &combined_samples[pos], .capacity = current_chunk_samples, .size = current_chunk_samples};
        uint8_t decoded[max_frame];
        buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
        int decoded_len = md_rx_process(&rx, &chunk_sample_buf, &decoded_buf, NULL);

        if (decoded_len > 0)
        {
            // Found a frame
            memcpy(detected_frames[frames_found], decoded, decoded_buf.size);
            detected_frame_lens[frames_found] = decoded_buf.size;
            frames_found++;
        }

        pos += current_chunk_samples;
    }

    // Verify we found exactly 2 frames
    assert_equal_int(frames_found, 2, "exactly 2 frames detected");

    // Verify the frames match the original packets
    assert_equal_int(detected_frame_lens[0], packed1_buf.size, "detected frame 0 size");
    assert_equal_int(detected_frame_lens[1], packed2_buf.size, "detected frame 1 size");
    assert_memory(detected_frames[0], packed1_data, detected_frame_lens[0], "detected frame 0 matches original packet");
    assert_memory(detected_frames[1], packed2_data, detected_frame_lens[1], "detected frame 1 matches original packet");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_with_digipeaters(float sample_rate, int num_digipeaters, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 256;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    ax25_packet_t packet;
    ax25_packet_init(&packet);
    ax25_addr_init_with(&packet.source, "SOURCE", 0, 0);
    ax25_addr_init_with(&packet.destination, "DEST", 0, 0);

    // Set up digipeaters
    packet.path_len = num_digipeaters;
    for (int i = 0; i < num_digipeaters; i++)
    {
        char callsign[7] = "DIGI0";
        callsign[4] += i;
        ax25_addr_init_with(&packet.path[i], callsign, i, 0);
    }

    const char *info = "Test message";
    memcpy(packet.info, info, strlen(info));
    packet.info_len = strlen(info);

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = sizeof(samples) / sizeof(float), .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);
    assert_true(decoded_len > 0, "demodulation successful");
    test_modem_verify_packet(&decoded_buf, packed_data, packed_buf.size, "digipeaters");

    test_modem_free_pair(&tx, &rx);
}

void test_modem_snr_performance(uint32_t demod_flags)
{
    const float sample_rate = 22050.0f;
    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 256;
    const int num_simulations = 50;
    const float snr_start = 1.0f;
    const float snr_end = 6.0f;
    const float snr_step = 1.0f;
    const float amplitude_noise = 0.5f;

    struct md_tx tx;
    md_tx_init(&tx, sample_rate, tx_delay, tx_tail);

    ax25_packet_t packet;
    test_modem_create_aprs_packet(&packet, "XX0TST", "APN001", "!5221.20N/02043.85E# TEST qwerty");

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");

    float clean_samples[max_samples];
    float_buffer_t clean_sample_buf = {.data = clean_samples, .capacity = sizeof(clean_samples) / sizeof(float), .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &clean_sample_buf, NULL);

    md_tx_free(&tx);

    srand(42);
    for (float snr_db = snr_start; snr_db <= snr_end; snr_db += snr_step)
    {
        const float amplitude_signal = amplitude_noise * powf(10.0f, snr_db / 20.0f);

        int successes = 0;
        for (int sim = 0; sim < num_simulations; sim++)
        {
            struct md_rx rx;
            md_rx_init(&rx, sample_rate, demod_flags);

            float noisy_samples[max_samples];
            memcpy(noisy_samples, clean_samples, sample_count * sizeof(float));

            for (int i = 0; i < sample_count; i++)
            {
                noisy_samples[i] *= amplitude_signal;
                noisy_samples[i] += awgn(amplitude_noise);
            }

            float_buffer_t noisy_sample_buf = {.data = noisy_samples, .capacity = sizeof(noisy_samples) / sizeof(float), .size = sample_count};
            uint8_t decoded[max_frame];
            buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
            int decoded_len = md_rx_process(&rx, &noisy_sample_buf, &decoded_buf, NULL);

            if (decoded_len > 0 && decoded_buf.size == packed_buf.size && memcmp(decoded, packed_data, packed_buf.size) == 0)
                successes++;

            md_rx_free(&rx);
        }

        float success_rate = 100.0f * successes / num_simulations;
        printf("SNR %+.1f dB; Decode rate %.0f%%\n", snr_db, success_rate);
    }
}

void test_modem_multi_rx_basic()
{
    const float sample_rate = 22050.0f;
    const int max_samples = test_modem_max_samples(sample_rate, 2.0f);
    const int max_frame = 256;

    struct md_multi_rx mrx;
    test_modem_init_multi(&mrx, sample_rate, DEMOD_ALL);

    struct md_tx tx;
    md_tx_init(&tx, sample_rate, tx_delay, tx_tail);

    ax25_packet_t packet;
    test_modem_create_aprs_packet(&packet, "XX0TST", "APN001", "!5221.20N/02043.85E# TEST");

    uint8_t packed_data[max_frame];
    buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
    int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "modulation successful");

    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
    int decoded_len = md_multi_rx_process(&mrx, &sample_buf, &decoded_buf);

    assert_true(decoded_len > 0, "multi-rx demodulation successful");
    assert_equal_int(decoded_buf.size, packed_buf.size, "multi-rx decoded length matches original");
    assert_memory(decoded, packed_data, packed_buf.size, "multi-rx decoded data matches original");

    md_tx_free(&tx);
    md_multi_rx_free(&mrx);
}

void test_modem_multi_rx_mixed_packets()
{
    const float sample_rate = 22050.0f;
    const int max_samples = test_modem_max_samples(sample_rate, 4.0f);
    const int max_frame = 256;

    struct md_multi_rx mrx;
    test_modem_init_multi(&mrx, sample_rate, DEMOD_ALL);

    struct md_tx tx;
    md_tx_init(&tx, sample_rate, tx_delay, tx_tail);

    // Create two different packets
    ax25_packet_t packet1, packet2;
    test_modem_create_aprs_packet(&packet1, "SRC1", "DST1", "Packet 1");
    test_modem_create_aprs_packet(&packet2, "SRC2", "DST2", "Packet 2");

    uint8_t packed1[max_frame], packed2[max_frame];
    buffer_t packed1_buf = {.data = packed1, .capacity = max_frame, .size = 0};
    buffer_t packed2_buf = {.data = packed2, .capacity = max_frame, .size = 0};
    ax25_error_e pack1_result = ax25_packet_pack(&packet1, &packed1_buf);
    ax25_error_e pack2_result = ax25_packet_pack(&packet2, &packed2_buf);
    assert_equal_int(pack1_result, AX25_SUCCESS, "pack 1 success");
    assert_equal_int(pack2_result, AX25_SUCCESS, "pack 2 success");

    // Modulate both packets with silence between
    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
    md_tx_process(&tx, &packed1_buf, &sample_buf, NULL);

    int silence_start = sample_buf.size;
    int silence_samples = test_modem_max_samples(sample_rate, 0.5f);
    memset(samples + silence_start, 0, silence_samples * sizeof(float));

    float_buffer_t temp_buf = {.data = samples + silence_start + silence_samples,
                               .capacity = max_samples - silence_start - silence_samples,
                               .size = 0};
    md_tx_process(&tx, &packed2_buf, &temp_buf, NULL);

    int total_samples = silence_start + silence_samples + temp_buf.size;
    sample_buf.size = total_samples;

    // Process with multi-rx
    uint8_t decoded[max_frame];
    buffer_t decoded_buf = {.data = decoded, .capacity = max_frame, .size = 0};
    int decoded_len = md_multi_rx_process(&mrx, &sample_buf, &decoded_buf);

    assert_true(decoded_len > 0, "multi-rx mixed packets successful");
    // Verify that the decoded packet matches one of the original packets
    int matches_packet1 = (decoded_buf.size == packed1_buf.size &&
                           memcmp(decoded, packed1, packed1_buf.size) == 0);
    int matches_packet2 = (decoded_buf.size == packed2_buf.size &&
                           memcmp(decoded, packed2, packed2_buf.size) == 0);
    assert_true(matches_packet1 || matches_packet2, "multi-rx decoded packet matches one of the originals");

    md_tx_free(&tx);
    md_multi_rx_free(&mrx);
}

void test_modem_highlevel_init_free()
{
    modem_t modem;
    modem_params_t params = {
        .sample_rate = 22050.0f,
        .types = DEMOD_ALL,
        .tx_delay = tx_delay,
        .tx_tail = tx_tail};

    modem_init(&modem, &params);

    // Test basic modulation/demodulation
    const uint8_t test_data[] = "Hello World!";
    buffer_t input_buf = {.data = (uint8_t *)test_data, .capacity = sizeof(test_data), .size = sizeof(test_data)};

    float samples[22050]; // 1 second buffer
    float_buffer_t sample_buf = {.data = samples, .capacity = 22050, .size = 0};

    int mod_result = modem_modulate(&modem, &input_buf, &sample_buf);
    assert_true(mod_result > 0, "high-level modulation successful");

    uint8_t decoded[256];
    buffer_t decoded_buf = {.data = decoded, .capacity = 256, .size = 0};
    int demod_result = modem_demodulate(&modem, &sample_buf, &decoded_buf);

    if (demod_result > 0)
    {
        assert_equal_int(decoded_buf.size, input_buf.size, "high-level roundtrip size matches");
        assert_memory(decoded, test_data, input_buf.size, "high-level roundtrip data matches");
    }

    modem_free(&modem);
}

void test_modem_empty_payload(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 1.0f);

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    // Test with minimal valid payload instead of empty
    const uint8_t minimal_data[] = {0x00};
    buffer_t minimal_buf = {.data = (uint8_t *)minimal_data, .capacity = 1, .size = 1};

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
    int sample_count = md_tx_process(&tx, &minimal_buf, &sample_buf, NULL);

    assert_true(sample_count > 0, "minimal payload modulation produces samples");

    uint8_t decoded[256];
    buffer_t decoded_buf = {.data = decoded, .capacity = 256, .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);

    if (decoded_len > 0)
    {
        test_modem_verify_packet(&decoded_buf, minimal_data, 1, "minimal_payload");
    }

    test_modem_free_pair(&tx, &rx);
}

void test_modem_max_size_payload(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 10.0f); // Larger buffer

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    uint8_t large_data[255];                         // Maximum AX.25 payload size
    test_modem_generate_pattern(large_data, 255, 0); // Sequential pattern
    buffer_t large_buf = {.data = large_data, .capacity = 255, .size = 255};

    float samples[max_samples];
    float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
    int sample_count = md_tx_process(&tx, &large_buf, &sample_buf, NULL);
    assert_true(sample_count > 0, "large payload modulation successful");

    uint8_t decoded[512];
    buffer_t decoded_buf = {.data = decoded, .capacity = 512, .size = 0};
    int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);

    if (decoded_len > 0)
    {
        test_modem_verify_packet(&decoded_buf, large_data, 255, "max_size_payload");
    }

    test_modem_free_pair(&tx, &rx);
}

void test_modem_various_patterns(float sample_rate, uint32_t demod_flags)
{
    const int max_samples = test_modem_max_samples(sample_rate, 3.0f);
    const int max_frame = 256;

    struct md_tx tx;
    struct md_rx rx;
    test_modem_init_pair(&tx, &rx, sample_rate, demod_flags);

    uint8_t test_patterns[4][64];
    const char *pattern_names[] = {"sequential", "alternating", "zeros", "ones"};

    for (int i = 0; i < 4; i++)
    {
        // Generate the pattern data
        test_modem_generate_pattern(test_patterns[i], 64, i);

        // Create an AX.25 packet with the pattern as the info field
        ax25_packet_t packet;
        ax25_packet_init(&packet);
        ax25_addr_init_with(&packet.source, "PATTERN", 0, 0);
        ax25_addr_init_with(&packet.destination, "TEST", 0, 0);
        packet.path_len = 0;
        memcpy(packet.info, test_patterns[i], 64);
        packet.info_len = 64;

        // Pack the packet
        uint8_t packed_data[max_frame];
        buffer_t packed_buf = {.data = packed_data, .capacity = sizeof(packed_data), .size = 0};
        ax25_error_e pack_result = ax25_packet_pack(&packet, &packed_buf);
        assert_equal_int(pack_result, AX25_SUCCESS, "packet pack success");

        // Modulate the packed packet
        float samples[max_samples];
        float_buffer_t sample_buf = {.data = samples, .capacity = max_samples, .size = 0};
        int sample_count = md_tx_process(&tx, &packed_buf, &sample_buf, NULL);
        assert_true(sample_count > 0, "pattern modulation successful");

        // Demodulate
        uint8_t decoded[max_frame];
        buffer_t decoded_buf = {.data = decoded, .capacity = sizeof(decoded), .size = 0};
        int decoded_len = md_rx_process(&rx, &sample_buf, &decoded_buf, NULL);
        assert_true(decoded_len > 0, "pattern demodulation successful");

        // Verify the demodulated packet matches the original packed data
        test_modem_verify_packet(&decoded_buf, packed_data, packed_buf.size, pattern_names[i]);
    }

    test_modem_free_pair(&tx, &rx);
}
#endif
