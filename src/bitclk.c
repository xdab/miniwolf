#include "bitclk.h"
#include "common.h"
#include <math.h>

#define PHASE_MAX 1.0f
#define PHASE_MIN -1.0f
#define PHASE_WRAP 2.0f

#define MAX_INERTIA 0.82f
#define MIN_INERTIA 0.28f

#define GOOD_TRANSITION_THR 0.05f

#define DCD_ON_THR 26
#define DCD_OFF_THR 12

static float wrap_phase(float value)
{
    while (value >= PHASE_MAX)
        value -= PHASE_WRAP;
    while (value < PHASE_MIN)
        value += PHASE_WRAP;
    return value;
}

void bitclk_init(bitclk_t *bitclk, float sample_rate, float bit_rate)
{
    nonnull(bitclk, "bitclk");

    bitclk->pll_clock_tick = 2.0f * bit_rate / sample_rate;
    bitclk->pll_clock = 0.0f;
    bitclk->last_soft_bit = 0.0f;

    bitclk->transition_history = 0;
    bitclk->signal_quality = 0;
    bitclk->data_detect = 0;
}

static void update_pll_lock_detection(bitclk_t *bitclk, float timing_error_in_bit_periods)
{
    int good_transition = (fabsf(timing_error_in_bit_periods) < GOOD_TRANSITION_THR);

    bitclk->transition_history = (bitclk->transition_history << 1) | (good_transition ? 1 : 0);
    bitclk->signal_quality = __builtin_popcount(bitclk->transition_history);

    if (bitclk->signal_quality >= DCD_ON_THR && !bitclk->data_detect)
    {
        bitclk->data_detect = 1;
        LOGD("PLL locked");
    }
    else if (bitclk->signal_quality <= DCD_OFF_THR && bitclk->data_detect)
    {
        bitclk->data_detect = 0;
        LOGD("PLL unlocked");
    }
}

int bitclk_detect(bitclk_t *bitclk, float soft_bit)
{
    nonnull(bitclk, "bitclk");

    // Advance PLL
    float prev_pll_value = bitclk->pll_clock;
    bitclk->pll_clock = wrap_phase(bitclk->pll_clock + bitclk->pll_clock_tick);

    // Bit sampling when PLL wraps around +-1.0
    int sampled_bit = BITCLK_NONE;
    if (prev_pll_value > 0.0f && bitclk->pll_clock <= 0.0f)
        sampled_bit = (bitclk->last_soft_bit > 0.0f) ? 1 : 0;

    // Phase correction when softbit crosses 0.0
    if (bitclk->last_soft_bit * soft_bit < 0.0f)
    {
        // Calculate precise zero-crossing timing using linear interpolation
        float delta_soft_bit = soft_bit - bitclk->last_soft_bit;
        if (fabsf(delta_soft_bit) > 1e-6f)
        {
            // Fractional distance from last sample to crossing
            // 0.0 = crossing at last sample, 1.0 = crossing at current sample
            float fraction = -bitclk->last_soft_bit / delta_soft_bit;

            // Estimate PLL value at crossing time and compute timing error
            float pll_at_crossing = prev_pll_value + bitclk->pll_clock_tick * fraction;
            float timing_error_in_samples = -pll_at_crossing / bitclk->pll_clock_tick;
            float timing_error_in_bit_periods = timing_error_in_samples * bitclk->pll_clock_tick / 2.0f;
            update_pll_lock_detection(bitclk, timing_error_in_bit_periods);

            // Adaptive PLL inertia
            float inertia = MIN_INERTIA + (MAX_INERTIA - MIN_INERTIA) * (bitclk->signal_quality / 32.0f);
            float ideal_pll = (1.0f - fraction) * bitclk->pll_clock_tick;
            bitclk->pll_clock = bitclk->pll_clock * inertia + ideal_pll * (1.0f - inertia);
        }
    }

    bitclk->last_soft_bit = soft_bit;

    return sampled_bit;
}
