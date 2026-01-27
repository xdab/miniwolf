#include "test.h"
#include "test_ring.h"
#include "test_modem.h"
#include "test_mavg.h"

static const float sample_rates[] = {16000.0f, 22050.0f, 32000.0f, 44100.0f, 48000.0f};
static const uint32_t demod_flags[] = {DEMOD_GOERTZEL_OPTIM, DEMOD_QUADRATURE};

const char *demod_name(uint32_t flags)
{
    if (flags == DEMOD_GOERTZEL_OPTIM)
        return "Goertzel (Optimistic)";
    if (flags == DEMOD_GOERTZEL_PESIM)
        return "Goertzel (Pessimistic)";
    if (flags == DEMOD_QUADRATURE)
        return "Quadrature";
    return "Unknown";
}

int main(void)
{
    begin_suite();

    begin_module("Ring");
    test_ring_init();
    test_ring_write_read();
    test_ring_full();
    test_ring_read_empty();
    test_ring_wrap();
    test_ring_shift1_empty();
    test_ring_shift1_delay();
    end_module();

    begin_module("Moving Average");
    test_mavg_init();
    test_mavg_update_single();
    test_mavg_update_multiple();
    test_mavg_window_wrapping();
    test_mavg_get_empty();
    test_mavg_get_partial();
    test_mavg_free();
    test_ema_init();
    test_ema_update_first();
    test_ema_update_standard();
    test_ema_get_uninitialized();
    test_ema_free();
    end_module();

    begin_module("Bell202");
    for (int j = 0; j < sizeof(demod_flags) / sizeof(demod_flags[0]); j++)
    {
        demod_type_t demod_flag = demod_flags[j];
        printf("Demodulation algorithm: %s\n", demod_name(demod_flag));

        for (int i = 0; i < sizeof(sample_rates) / sizeof(sample_rates[0]); i++)
        {
            struct md_rx rx;
            struct md_tx tx;
            float sample_rate = sample_rates[i];

            md_rx_init(&rx, sample_rate, demod_flag);
            md_tx_init(&tx, sample_rate, tx_delay, tx_tail);
            test_modem_aprs_like(sample_rate, demod_flag);
            test_modem_arbitrary_data(sample_rate, 64, demod_flag);
            test_modem_arbitrary_data(sample_rate, 128, demod_flag);
            test_modem_arbitrary_data(sample_rate, 256, demod_flag);
            test_modem_with_digipeaters(sample_rate, 0, demod_flag);
            test_modem_with_digipeaters(sample_rate, 1, demod_flag);
            test_modem_with_digipeaters(sample_rate, 4, demod_flag);
            test_modem_with_noise_around_packet(sample_rate, demod_flag);
            test_modem_with_noise_over_packet(sample_rate, demod_flag);
            test_modem_two_packets_back_to_back(sample_rate, demod_flag);
            test_modem_empty_payload(sample_rate, demod_flag);
            test_modem_max_size_payload(sample_rate, demod_flag);
            test_modem_various_patterns(sample_rate, demod_flag);
            md_rx_free(&rx);
            md_tx_free(&tx);
        }

        test_modem_snr_performance(demod_flag);
    }
    // Multi-receiver and high-level API tests
    test_modem_multi_rx_basic();
    test_modem_multi_rx_mixed_packets();
    test_modem_highlevel_init_free();
    end_module();

    int failed = end_suite();

    return failed ? 1 : 0;
}