#pragma once

static inline void nrzi_encoder_init(int *nrzi_bit)
{
    *nrzi_bit = 0;
}

static inline void nrzi_decoder_init(int *last_bit)
{
    *last_bit = 0;
}


static inline int nrzi_encode(int b, int *nrzi_bit)
{
    if (!b)
        *nrzi_bit ^= 1;
    return *nrzi_bit;
}

static inline int nrzi_decode(int b, int *last_bit)
{
    int output_bit = (b == *last_bit);
    *last_bit = b;
    return output_bit;
}
