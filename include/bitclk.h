#pragma once

#include <stdint.h>

#define BITCLK_NONE -1

typedef struct bitclk_pll
{
    float last_soft_bit;

    float pll_clock;      // Phase accumulator
    float pll_clock_tick; // Step size per audio sample

    uint32_t transition_history;
    int signal_quality;
    int data_detect;

} bitclk_t;

void bitclk_init(bitclk_t *detector, float sample_rate, float bit_rate);

int bitclk_detect(bitclk_t *detector, float soft_bit);
