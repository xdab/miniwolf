#include <string.h>
#include <ctype.h>
#include "conf.h"

static conf_error_e conf_trim(char *str)
{
    size_t len;
    char *end;
    char *start = str;

    nonnull(str, "str");

    if (*start == '\0')
    {
        str[0] = '\0';
        return CONF_SUCCESS;
    }

    len = strlen(start);
    end = start + len - 1;

    while (end > start && isspace((unsigned char)*end))
        end--;

    end[1] = '\0';

    if (start != str)
        memmove(str, start, strlen(start) + 1);

    return CONF_SUCCESS;
}

static conf_error_e conf_parse_line(conf_entry_t *entry, const char *line)
{
    const char *eq;
    size_t key_len;
    size_t val_len;

    nonnull(entry, "entry");
    nonnull(line, "line");

    while (isspace((unsigned char)*line))
        line++;
    if (*line == '#' || *line == '\0')
        return CONF_SUCCESS;

    eq = strchr(line, '=');
    if (!eq)
        return -CONF_ERR_INVALID_VALUE;

    key_len = eq - line;
    if (key_len >= CONF_KEY_SIZE)
        key_len = CONF_KEY_SIZE - 1;
    memcpy(entry->key, line, key_len);
    entry->key[key_len] = '\0';
    conf_trim(entry->key);

    val_len = strlen(line) - key_len - 1;
    if (val_len >= CONF_VAL_SIZE)
        val_len = CONF_VAL_SIZE - 1;
    memcpy(entry->value, eq + 1, val_len);
    entry->value[val_len] = '\0';
    conf_trim(entry->value);

    return CONF_SUCCESS;
}

conf_error_e conf_load(conf_t *conf, const char *filename)
{
    FILE *fp;
    char line[512];
    conf_entry_t *entry;

    nonnull(conf, "conf");
    nonnull(filename, "filename");

    _assert(sizeof(line) >= CONF_KEY_SIZE + CONF_VAL_SIZE, "line buffer fits config entry");

    fp = fopen(filename, "r");
    if (!fp)
        return -CONF_ERR_FILE_NOT_FOUND;

    conf->count = 0;
    while (fgets(line, sizeof(line), fp))
    {
        if (conf->count >= CONF_MAX_ENTRIES)
        {
            fclose(fp);
            return -CONF_ERR_TOO_MANY_ENTRIES;
        }
        entry = &conf->entries[conf->count];
        if (conf_parse_line(entry, line) == CONF_SUCCESS)
        {
            if (entry->key[0] != '\0')
                conf->count++;
        }
    }

    fclose(fp);
    return CONF_SUCCESS;
}

conf_error_e conf_get_int(const conf_t *conf, const char *key, int *out)
{
    int i;
    nonnull(conf, "conf");
    nonnull(key, "key");
    nonnull(out, "out");

    for (i = 0; i < conf->count; i++)
    {
        if (strcmp(conf->entries[i].key, key) == 0)
        {
            *out = atoi(conf->entries[i].value);
            return CONF_SUCCESS;
        }
    }
    return -CONF_ERR_NOT_FOUND;
}

conf_error_e conf_get_float(const conf_t *conf, const char *key, float *out)
{
    int i;
    nonnull(conf, "conf");
    nonnull(key, "key");
    nonnull(out, "out");

    for (i = 0; i < conf->count; i++)
    {
        if (strcmp(conf->entries[i].key, key) == 0)
        {
            *out = atof(conf->entries[i].value);
            return CONF_SUCCESS;
        }
    }
    return -CONF_ERR_NOT_FOUND;
}

conf_error_e conf_get_bool(const conf_t *conf, const char *key, int *out)
{
    int i;
    nonnull(conf, "conf");
    nonnull(key, "key");
    nonnull(out, "out");

    for (i = 0; i < conf->count; i++)
    {
        if (strcmp(conf->entries[i].key, key) == 0)
        {
            if (strcmp(conf->entries[i].value, "true") == 0)
                *out = 1;
            else if (strcmp(conf->entries[i].value, "false") == 0)
                *out = 0;
            else
                return -CONF_ERR_INVALID_VALUE;
            return CONF_SUCCESS;
        }
    }
    return -CONF_ERR_NOT_FOUND;
}

const char *conf_get_str(const conf_t *conf, const char *key)
{
    int i;
    nonnull(conf, "conf");
    nonnull(key, "key");

    for (i = 0; i < conf->count; i++)
        if (strcmp(conf->entries[i].key, key) == 0)
            return conf->entries[i].value;

    return NULL;
}

int conf_get_int_or_default(const conf_t *conf, const char *key, int def)
{
    int out;

    if (conf_get_int(conf, key, &out) == CONF_SUCCESS)
        return out;

    return def;
}

float conf_get_float_or_default(const conf_t *conf, const char *key, float def)
{
    float out;

    if (conf_get_float(conf, key, &out) == CONF_SUCCESS)
        return out;

    return def;
}

int conf_get_bool_or_default(const conf_t *conf, const char *key, int def)
{
    int out;

    if (conf_get_bool(conf, key, &out) == CONF_SUCCESS)
        return out;

    return def;
}

const char *conf_get_str_or_default(const conf_t *conf, const char *key, const char *def)
{
    const char *out = conf_get_str(conf, key);
    return (out != NULL) ? out : def;
}
