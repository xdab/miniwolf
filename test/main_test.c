#include "test.h"
#include "test_ax25.h"
#include "test_tnc2.h"
#include "test_ring.h"
#include "test_hldc.h"
#include "test_modem.h"
#include "test_kiss.h"
#include "test_line.h"
#include "test_tcp.h"
#include "test_udp.h"
#include "test_mavg.h"
#include "test_dedupe.h"

static const float sample_rates[] = {8000.0f, 11025.0f, 16000.0f, 22050.0f, 32000.0f, 44100.0f, 48000.0f};
static const uint32_t demod_flags[] = {DEMOD_GOERTZEL_OPTIM, DEMOD_QUADRATURE};

const char *demod_name(uint32_t flags)
{
    if (flags == DEMOD_GOERTZEL_OPTIM)
        return "Goertzel (Optimistic)";
    if (flags == DEMOD_GOERTZEL_PESIM)
        return "Goertzel (Pessimistic)";
    if (flags == DEMOD_QUADRATURE)
        return "Quadrature";
    if (flags == DEMOD_SPLIT_MARK)
        return "Split-filter (Mark)";
    if (flags == DEMOD_SPLIT_SPACE)
        return "Split-filter (Space)";
    if (flags == DEMOD_RRC)
        return "RRC (Direwolf)";
    return "Unknown";
}

int main(void)
{
    begin_suite();

    begin_module("Address");
    test_addr_init();
    test_addr_init_with();
    test_addr_pack();
    test_addr_unpack();
    end_module();

    begin_module("Packet");
    test_packet_init();
    test_packet_pack();
    test_packet_pack_unpack();
    end_module();

    begin_module("TNC2");
    test_tnc2_string_to_packet_with_repeated();
    test_tnc2_roundtrip_simple();
    test_tnc2_roundtrip_complex();
    test_tnc2_invalid_chars_in_callsign();
    test_tnc2_control_char();
    test_tnc2_ssid_overflow();
    test_tnc2_missing_greater_than();
    test_tnc2_missing_colon();
    test_tnc2_empty_callsign();
    test_tnc2_too_many_digis();
    test_tnc2_callsign_too_long();
    test_tnc2_space_in_callsign();
    test_tnc2_info_too_large();
    test_tnc2_null_termination();
    test_tnc2_packet_roundtrip_with_null_term();
    test_tnc2_edge_case_mixed_valid_invalid_chars();
    test_tnc2_edge_case_boundary_digits();
    test_tnc2_edge_case_callsign_padding();
    end_module();

    begin_module("Ring");
    test_ring_init();
    test_ring_write_read();
    test_ring_full();
    test_ring_read_empty();
    test_ring_wrap();
    test_ring_shift1_empty();
    test_ring_shift1_delay();
    end_module();

    begin_module("HLDC");
    test_hldc_framer_init();
    test_hldc_framer_flag_scaling();
    test_hldc_framer_bit_stuffing();
    test_hldc_deframer_init();
    end_module();

    begin_module("KISS");
    test_kiss_decoder_init();
    test_kiss_decoder_empty_frames();
    test_kiss_decoder_data_frame();
    test_kiss_encode_basic();
    test_kiss_encode_with_escaping();
    test_kiss_read_frame_escaped_characters();
    test_kiss_read_invalid_escape_sequence();
    test_kiss_read_incomplete_frame();
    test_kiss_read_consecutive_empty_frames();
    test_kiss_read_multiple_consecutive_escape();
    test_kiss_read_back_to_back_frames();
    end_module();

    begin_module("Line Reader");
    test_lr_simple_line();
    test_lr_crlf_handling();
    test_lr_empty_lines_ignored();
    test_lr_embedded_cr();
    test_lr_multiple_lines();
    test_lr_binary_data();
    test_lr_line_too_long();
    end_module();

    begin_module("TCP");
    test_tcp_server_init_valid();
    test_tcp_server_process_timeout();
    test_tcp_server_accept_client();
    test_tcp_server_read_data();
    test_tcp_server_client_disconnect();
    test_tcp_server_broadcast();
    test_tcp_server_free();
    test_tcp_client_init_valid();
    test_tcp_client_init_invalid_address();
    test_tcp_client_process_timeout();
    test_tcp_client_connect_and_read();
    test_tcp_client_server_disconnect();
    test_tcp_client_read_error();
    test_tcp_client_partial_read();
    test_tcp_client_connection_in_progress();
    test_tcp_client_write_error();
    test_tcp_client_free();
    end_module();

    begin_module("UDP");
    test_udp_init_valid();
    test_udp_init_broadcast();
    test_udp_send_unicast();
    test_udp_send_broadcast();
    test_udp_free();
    test_udp_init_invalid_address();
    test_udp_send_error_handling();
    test_udp_send_broadcast_config();
    test_udp_server_init_valid();
    test_udp_server_process_timeout();
    test_udp_server_receive_data();
    test_udp_server_free();
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

    begin_module("Dedupe");
    test_dedupe_init();
    test_dedupe_first_frame_not_duplicate();
    test_dedupe_duplicate_within_expiration();
    test_dedupe_duplicate_after_expiration();
    test_dedupe_different_crc_not_duplicate();
    test_dedupe_slot_eviction();
    test_dedupe_crc_at_all_indices();
    test_dedupe_time_boundary();
    test_dedupe_expired_crc_reuse_slot();
    test_dedupe_rapid_same_crc();
    test_dedupe_multiple_expirations();
    test_dedupe_timestamp_bumping();
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
