#include "bitclk.h"
#include "common.h"
#include <math.h>

#define PHASE_MAX 1.0f
#define PHASE_MIN -1.0f
#define PHASE_WRAP 2.0f

#define DCD_GOOD_THRESHOLD 0.10f
#define DCD_THRESH_ON 28
#define DCD_THRESH_OFF 12

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

    bitclk->good_hist = 0;
    bitclk->bad_hist = 0;
    bitclk->score = 0;
    bitclk->data_detect = 0;

    bitclk->pll_locked_inertia = 0.75f;
    bitclk->pll_searching_inertia = 0.50f;
}

static void update_pll_lock_detection(bitclk_t *bitclk)
{
    int transition_near_zero = (fabsf(bitclk->pll_clock) < DCD_GOOD_THRESHOLD);

    // Update transition history and current score
    bitclk->good_hist = (bitclk->good_hist << 1) | (transition_near_zero ? 1 : 0);
    bitclk->bad_hist = (bitclk->bad_hist << 1) | (transition_near_zero ? 0 : 1);
    bitclk->score = (bitclk->score << 1) | ((__builtin_popcount(bitclk->good_hist) - __builtin_popcount(bitclk->bad_hist)) >= 2 ? 1 : 0);

    int good_count = __builtin_popcount(bitclk->score);
    if (good_count >= DCD_THRESH_ON && !bitclk->data_detect)
    {
        bitclk->data_detect = 1;
        LOGD("PLL locked");
    }
    else if (good_count <= DCD_THRESH_OFF && bitclk->data_detect)
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
        sampled_bit = (soft_bit > 0.0f) ? 1 : 0;

    // Phase correction when softbit crosses 0.0
    if (bitclk->last_soft_bit * soft_bit < 0.0f)
    {
        update_pll_lock_detection(bitclk);

        // Calculate precise zero-crossing timing using linear interpolation
        float delta_soft_bit = soft_bit - bitclk->last_soft_bit;
        if (fabsf(delta_soft_bit) > 1e-6f)
        {
            float fraction = -bitclk->last_soft_bit / delta_soft_bit;
            float target_phase = bitclk->pll_clock_tick * fraction;

            // Adaptive PLL inertia
            float inertia = bitclk->data_detect ? bitclk->pll_locked_inertia : bitclk->pll_searching_inertia;
            float new_pll = bitclk->pll_clock * inertia + target_phase * (1.0f - inertia);
            bitclk->pll_clock = wrap_phase(new_pll);
        }
    }

    bitclk->last_soft_bit = soft_bit;

    return sampled_bit;
}
