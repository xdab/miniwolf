#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "common.h"
#include "options.h"
#include "audio.h"
#include "loop.h"
#include "miniwolf.h"

static void list_devices(void)
{
    void **hints;

    if (snd_device_name_hint(-1, "pcm", &hints) < 0)
    {
        LOG("failed to get device hints");
        return;
    }

    printf("%-5s%-60s\n", "Cap", "Name");

    for (void **n = hints; *n != NULL; n++)
    {
        char *name = snd_device_name_get_hint(*n, "NAME");
        if (!name)
            continue;

        char *io = snd_device_name_get_hint(*n, "IOID");

        char cap[3] = "--";
        if (io == NULL)
        {
            cap[0] = 'I';
            cap[1] = 'O';
        }
        else if (strcmp(io, "Input") == 0)
            cap[0] = 'I';
        else if (strcmp(io, "Output") == 0)
            cap[1] = 'O';

        printf("%-5s%-60s\n", cap, name);

        free(name);
        free(io);
    }

    snd_device_name_free_hint(hints);
}

int main(int argc, char *argv[])
{
    options_t opts = {0};
    opts_init(&opts);
    opts_parse_args(&opts, argc, argv);
    opts_parse_conf_file(&opts, opts.config_file);
    opts_defaults(&opts);

    _log_level = opts.log_level;

    if (aud_initialize())
        EXIT("Failed to initialize audio subsystem");

    if (opts.list)
    {
        list_devices();
        goto NICE_EXIT;
    }

    if (opts.dev_name[0] == '\0')
        EXIT("No input specified");

    if (opts.rate <= 0)
        EXIT("Invalid sample rate specified");

    LOG("Using device '%s'", opts.dev_name);

    if (aud_configure(opts.dev_name, opts.rate, opts.dev_input, opts.dev_output))
        EXIT("Failed to configure sound device");

    miniwolf_init(&g_miniwolf, &opts);

    if (opts.noop)
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
