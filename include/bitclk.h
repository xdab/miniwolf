#pragma once

#include <stdint.h>

#define BITCLK_NONE -1

typedef struct bitclk_pll
{
    float last_soft_bit;

    float pll_clock;      // Phase accumulator
    float pll_clock_tick; // Step size per audio sample

    uint32_t good_hist; // History of good transition flags
    uint32_t bad_hist;  // History of bad transition flags
    uint32_t score;     // Rolling score for hysteresis (good transitions among last 32)
    int data_detect;    // PLL lock status

    float pll_locked_inertia;    // Conservative corrections when locked
    float pll_searching_inertia; // Aggressive corrections when searching

} bitclk_t;

void bitclk_init(bitclk_t *detector, float sample_rate, float bit_rate);

int bitclk_detect(bitclk_t *detector, float soft_bit);
