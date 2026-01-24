#include "bitclk.h"
#include "common.h"
#include <math.h>

// PLL constants
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

void bitclk_init(bitclk_t *detector, float sample_rate, float bit_rate)
{
    nonnull(detector, "detector");

    detector->pll_step_per_sample = 2.0f * bit_rate / sample_rate;
    detector->data_clock_pll = 0.0f;
    detector->prev_demod_output = 0.0f;

    detector->good_hist = 0;
    detector->bad_hist = 0;
    detector->score = 0;
    detector->data_detect = 0;

    detector->pll_locked_inertia = 0.775f;
    detector->pll_searching_inertia = 0.50f;
}

static void update_pll_lock_detection(bitclk_t *detector)
{
    // Check if the transition occurred near the expected sampling time
    int transition_near_zero = (fabsf(detector->data_clock_pll) < DCD_GOOD_THRESHOLD);

    // Update transition history
    detector->good_hist = (detector->good_hist << 1) | (transition_near_zero ? 1 : 0);
    detector->bad_hist = (detector->bad_hist << 1) | (transition_near_zero ? 0 : 1);

    // Update rolling score (good transitions minus bad transitions in 32-sample window)
    detector->score = (detector->score << 1) | ((__builtin_popcount(detector->good_hist) - __builtin_popcount(detector->bad_hist)) >= 2 ? 1 : 0);

    // Apply hysteresis for lock/unlock decisions
    int good_count = __builtin_popcount(detector->score);

    if (good_count >= DCD_THRESH_ON && !detector->data_detect)
    {
        detector->data_detect = 1; // Lock acquired
        LOGV("PLL locked");
    }
    else if (good_count <= DCD_THRESH_OFF && detector->data_detect)
    {
        detector->data_detect = 0; // Lock lost
        LOGV("PLL unlocked");
    }
}

int bitclk_detect(bitclk_t *detector, float soft_bit)
{
    nonnull(detector, "detector");

    int sampled_bit = BITCLK_NONE;
    float prev_pll_value = detector->data_clock_pll;

    // Advance PLL phase accumulator with wrapping
    detector->data_clock_pll = wrap_phase(detector->data_clock_pll + detector->pll_step_per_sample);

    // Check for crossing from positive to negative (bit sampling instant)
    if (prev_pll_value > 0.0f && detector->data_clock_pll < 0.0f)
    {
        // Sample the current soft bit
        sampled_bit = (soft_bit > 0.0f) ? 1 : 0;
    }

    // Detect zero crossings for phase correction
    if ((detector->prev_demod_output < 0.0f && soft_bit > 0.0f) ||
        (detector->prev_demod_output > 0.0f && soft_bit < 0.0f))
    {
        // Update lock detection based on corrected PLL state
        update_pll_lock_detection(detector);

        // Calculate precise zero-crossing timing using linear interpolation
        float denominator = soft_bit - detector->prev_demod_output;
        if (fabsf(denominator) > 1e-6f)
        {
            float fraction = -detector->prev_demod_output / denominator;
            float target_phase = detector->pll_step_per_sample * fraction;

            // Apply phase correction with adaptive inertia
            float inertia = detector->data_detect ? detector->pll_locked_inertia : detector->pll_searching_inertia;

            float new_pll = detector->data_clock_pll * inertia + target_phase * (1.0f - inertia);
            detector->data_clock_pll = wrap_phase(new_pll);
        }
    }

    // Update previous values for next iteration
    detector->prev_demod_output = soft_bit;

    return sampled_bit;
}
