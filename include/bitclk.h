#pragma once

#include <stdint.h>

#define BITCLK_NONE -1

typedef struct bitclk_pll
{
    // Core PLL state - float phase accumulator in [-1.0, 1.0) range
    float data_clock_pll;      // Phase accumulator
    float pll_step_per_sample; // Step size per audio sample

    // Lock detection with hysteresis
    uint32_t good_hist; // History of good transition flags
    uint32_t bad_hist;  // History of bad transition flags
    uint32_t score;     // Rolling score for hysteresis (good transitions in window)
    int data_detect;    // PLL lock status

    // Adaptive behavior parameters
    float pll_locked_inertia;    // Conservative correction when locked (0.74)
    float pll_searching_inertia; // Aggressive correction when searching (0.50)

    // Signal quality tracking
    float prev_demod_output; // Previous demodulator output for transition detection

} bitclk_t;

void bitclk_init(bitclk_t *detector, float sample_rate, float bit_rate);

int bitclk_detect(bitclk_t *detector, float soft_bit);
