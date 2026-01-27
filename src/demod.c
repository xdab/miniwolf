#include "demod.h"
#include "common.h"

void demod_init(demod_t *demod, demod_type_t type, demod_params_t *params)
{
    nonnull(demod, "demod");
    nonzero(type, "type");
    nonnull(params, "params");

    switch (type)
    {
    case DEMOD_GOERTZEL_OPTIM:
        demod_grz_init(&demod->impl.grz, params, &grz_params_optim);
        break;
    case DEMOD_GOERTZEL_PESIM:
        demod_grz_init(&demod->impl.grz, params, &grz_params_pesim);
        break;
    case DEMOD_QUADRATURE:
        demod_quad_init(&demod->impl.quad, params, &quad_params_default);
        break;
    default:
        EXIT("Unsupported demod type %d", type);
    }
    demod->type = type;
}

float demod_process(demod_t *demod, float sample)
{
    nonnull(demod, "demod");

    switch (demod->type)
    {
    case DEMOD_GOERTZEL_OPTIM:
    case DEMOD_GOERTZEL_PESIM:
        return demod_grz_process(&demod->impl.grz, sample);
    case DEMOD_QUADRATURE:
        return demod_quad_process(&demod->impl.quad, sample);
    default:
        EXIT("Unsupported demod type %d", demod->type);
    }
}

void demod_free(demod_t *demod)
{
    nonnull(demod, "demod");

    switch (demod->type)
    {
    case DEMOD_GOERTZEL_OPTIM:
    case DEMOD_GOERTZEL_PESIM:
        demod_grz_free(&demod->impl.grz);
        break;
    case DEMOD_QUADRATURE:
        demod_quad_free(&demod->impl.quad);
        break;
    default:
        EXIT("Unsupported demod type %d", demod->type);
    }
}
