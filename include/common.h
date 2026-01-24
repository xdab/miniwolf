#pragma once

#include <stdlib.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum
{
    LOG_LEVEL_STANDARD = 0,
    LOG_LEVEL_VERBOSE = 10,
    LOG_LEVEL_DEBUG = 20
} log_level_e;

extern log_level_e _log_level;
extern int _func_pad;

// General informational messages that should always be visible (with function prefix).
#define LOG(str, ...)                                                                    \
    {                                                                                    \
        fprintf(stderr, "i | %*s | " str "\n", _func_pad, __FUNCTION__, ##__VA_ARGS__); \
    }

// Detailed informational messages visible only with the verbose logging mode.
#define LOGV(str, ...)                                                                       \
    {                                                                                        \
        if (_log_level >= LOG_LEVEL_VERBOSE)                                                 \
            fprintf(stderr, "v | %*s | " str "\n", _func_pad, __FUNCTION__, ##__VA_ARGS__); \
    }

// Extremely detailed debugging messages visible only with the debug logging mode.
#define LOGD(str, ...)                                                                       \
    {                                                                                        \
        if (_log_level >= LOG_LEVEL_DEBUG)                                                   \
            fprintf(stderr, "d | %*s | " str "\n", _func_pad, __FUNCTION__, ##__VA_ARGS__); \
    }

// Critical errors requiring formatted output. Prints str (prefixed by function name) to stderr and terminates.
#define EXIT(str, ...)           \
    {                            \
        LOG(str, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);      \
    }

// Conditional checks that, if failed, should terminate the program. Prints a formatted message to stderr.
#define EXITIF(cond, exit_code, ...)  \
    if (cond)                         \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(exit_code);              \
    }

// Input validation: if value is NULL, prints an error message to stderr (including value_name, __FILE__, __LINE__) and exits with code -2.
#define nonnull(value, value_name) \
    EXITIF((void *)(value) == NULL, -2, "\n%s must not be NULL (%s:%d)\n\n", value_name, __FILE__, __LINE__);

// Input validation: if value is 0, prints an error message to stderr (including value_name, __FILE__, __LINE__) and exits with code -3.
#define nonzero(value, value_name) \
    EXITIF((value) == 0, -3, "\n%s must not be zero (%s:%d)\n\n", value_name, __FILE__, __LINE__);

// Assertion: if condition is false, prints an error message to stderr (including __FILE__, __LINE__) and exits with code -4.
#define _assert(condition, message) \
    EXITIF((condition) == 0, -4, "\n%s (%s:%d)\n\n", message, __FILE__, __LINE__);

// Conditional assignment: if value equals a, replace with b
#define REPLACE_IF_a_WITH_b(value, a, b) \
    if ((value) == (a))                  \
    {                                    \
        (value) = (b);                   \
    }
