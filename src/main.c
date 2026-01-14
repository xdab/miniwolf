#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "audio.h"
#include "args.h"
#include "loop.h"
#include "miniwolf.h"

int main(int argc, char *argv[])
{
    struct arguments args = {0};
    args_parse(argc, argv, &args);
    _log_level = args.log_level;

    if (aud_initialize())
        EXIT("Failed to initialize audio subsystem");

    if (args.list)
    {
        aud_list_devices();
        goto NICE_EXIT;
    }

    if (!args.dev_name)
        EXIT("No input specified");

    if (args.rate <= 0)
        EXIT("Invalid sample rate specified");

    LOG("Using device '%s'", args.dev_name);

    if (aud_configure(args.dev_name, args.rate, args.dev_input, args.dev_output))
        EXIT("Failed to configure sound device");

    miniwolf_init(&g_miniwolf, &args);

    if (args.noop)
    {
        LOG("No-op mode, exiting");
        goto NICE_EXIT;
    }

    if (aud_start())
        EXIT("Failed to start audio streams");

    loop_run(&g_miniwolf);

NICE_EXIT:
    miniwolf_free(&g_miniwolf);
    aud_terminate();
    return EXIT_SUCCESS;
}
