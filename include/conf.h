#pragma once

#include <stdio.h>
#include "common.h"

typedef enum
{
    CONF_SUCCESS = 0,
    CONF_ERR_NOT_FOUND,
    CONF_ERR_INVALID_VALUE,
    CONF_ERR_TOO_MANY_ENTRIES,
    CONF_ERR_FILE_NOT_FOUND,
} conf_error_e;

#define CONF_KEY_SIZE 64
#define CONF_VAL_SIZE 256

typedef struct conf_entry
{
    char key[CONF_KEY_SIZE];
    char value[CONF_VAL_SIZE];
} conf_entry_t;

#define CONF_MAX_ENTRIES 64

typedef struct conf
{
    conf_entry_t entries[CONF_MAX_ENTRIES];
    int count;
} conf_t;

conf_error_e conf_load(conf_t *conf, const char *filename);

conf_error_e conf_get_int(const conf_t *conf, const char *key, int *out);

conf_error_e conf_get_float(const conf_t *conf, const char *key, float *out);

conf_error_e conf_get_bool(const conf_t *conf, const char *key, int *out);

const char *conf_get_str(const conf_t *conf, const char *key);

int conf_get_int_or_default(const conf_t *conf, const char *key, int def);

float conf_get_float_or_default(const conf_t *conf, const char *key, float def);

int conf_get_bool_or_default(const conf_t *conf, const char *key, int def);

const char *conf_get_str_or_default(const conf_t *conf, const char *key, const char *def);
