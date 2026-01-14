#ifndef MAVG_H
#define MAVG_H

typedef struct mavg
{
    float *values;
    int capacity;
    int count;
    int index;
    float sum;
} mavg_t;

typedef struct ema
{
    float current_value;
    float alpha;
} ema_t;

void mavg_init(mavg_t *ma, int window_size);
float mavg_update(mavg_t *ma, float value);
float mavg_get(mavg_t *ma);
void mavg_free(mavg_t *ma);

void ema_init(ema_t *ema, int time_constant_samples);
float ema_update(ema_t *ema, float value);
float ema_get(ema_t *ema);
void ema_free(ema_t *ema);

#endif
