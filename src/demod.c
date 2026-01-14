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
        demod_quad_init(&demod->impl.quad, params);
        break;
    case DEMOD_SPLIT_MARK:
        demod_split_init(&demod->impl.split, params, &split_params_mark);
        break;
    case DEMOD_SPLIT_SPACE:
        demod_split_init(&demod->impl.split, params, &split_params_space);
        break;
    case DEMOD_RRC:
        demod_rrc_init(&demod->impl.rrc, params, &rrc_params_default);
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
    case DEMOD_SPLIT_MARK:
    case DEMOD_SPLIT_SPACE:
        return demod_split_process(&demod->impl.split, sample);
    case DEMOD_RRC:
        return demod_rrc_process(&demod->impl.rrc, sample);
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
    case DEMOD_SPLIT_MARK:
    case DEMOD_SPLIT_SPACE:
        demod_split_free(&demod->impl.split);
        break;
    case DEMOD_RRC:
        demod_rrc_free(&demod->impl.rrc);
        break;
    default:
        EXIT("Unsupported demod type %d", demod->type);
    }
}
