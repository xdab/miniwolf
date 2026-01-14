#pragma once

typedef struct agc
{
    float attack;
    float release;
    float envelope;
} agc_t;

void agc_init(agc_t *agc, float attack_ms, float release_ms, float sample_rate);

float agc_filter(agc_t *agc, float sample);

typedef struct agc2
{
    float attack;
    float release;
    float lower;
    float upper;
} agc2_t;

void agc2_init(agc2_t *agc, float attack_ms, float release_ms, float sample_rate);

float agc2_filter(agc2_t *agc, float sample);
