#include "bitclk.h"
#include "common.h"
#include <math.h>

// PLL constants
#define TICKS_PER_PLL_CYCLE 4294967296LL // 2^32 for high resolution
#define DCD_THRESH_ON 30                 // Lock when >= 30/32 good transitions
#define DCD_THRESH_OFF 6                 // Unlock when <= 6/32 good transitions
#define DCD_GOOD_WIDTH 524288LL          // ±512k units timing window for good transitions

void bitclk_init(bitclk_t *detector, float sample_rate, float bit_rate)
{
    nonnull(detector, "detector");

    // Calculate PLL step size: TICKS_PER_PLL_CYCLE * bit_rate / sample_rate
    // For 1200 baud AFSK, this gives high precision timing
    detector->pll_step_per_sample = (int32_t)((TICKS_PER_PLL_CYCLE * bit_rate) / sample_rate);

    // Initialize PLL state
    detector->data_clock_pll = 0;
    detector->prev_demod_output = 0.0f;

    // Initialize lock detection
    detector->good_hist = 0;
    detector->bad_hist = 0;
    detector->score = 0;
    detector->data_detect = 0;

    // Set inertia values for 1200 baud AFSK (matching Direwolf)
    detector->pll_locked_inertia = 0.74f;
    detector->pll_searching_inertia = 0.50f;
}

static void update_pll_lock_detection(bitclk_t *detector)
{
    // Check if the transition occurred near the expected sampling time
    int transition_near_zero = (llabs(detector->data_clock_pll) < DCD_GOOD_WIDTH);

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
    int32_t prev_pll_value = detector->data_clock_pll;

    // Advance PLL phase accumulator
    detector->data_clock_pll += detector->pll_step_per_sample;

    // Check for overflow (bit sampling instant)
    if (prev_pll_value > 0 && detector->data_clock_pll < 0)
    {
        // Sample the current soft bit
        sampled_bit = (soft_bit > 0.0f) ? 1 : 0;

        // Update lock detection based on transition timing
        update_pll_lock_detection(detector);
    }

    // Detect zero crossings for phase correction
    if ((detector->prev_demod_output < 0 && soft_bit > 0) ||
        (detector->prev_demod_output > 0 && soft_bit < 0))
    {

        // Calculate precise zero-crossing timing using linear interpolation
        float denominator = soft_bit - detector->prev_demod_output;
        if (fabsf(denominator) > 1e-6f)
        {
            float fraction = -detector->prev_demod_output / denominator;
            float target_phase = detector->pll_step_per_sample * fraction;

            // Apply phase correction with adaptive inertia
            float inertia = detector->data_detect ? detector->pll_locked_inertia : detector->pll_searching_inertia;

            detector->data_clock_pll = (int32_t)(detector->data_clock_pll * inertia + target_phase * (1.0f - inertia));
        }
    }

    // Update previous values for next iteration
    detector->prev_demod_output = soft_bit;

    return sampled_bit;
}
