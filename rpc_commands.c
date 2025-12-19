#include "module_loader.h"
#include "module_interface.h"
#include "rpc_commands.h"
#include "rpc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static module_loader_t *get_module_loader(void)
{
    return rpc_get_module_loader();
}

void rpc_commands_set_loader(module_loader_t *loader)
{
    (void)loader;
}

static int get_time_impl(struct timespec *ts)
{
    if (ts == NULL) {
        return -1;
    }
    return clock_gettime(CLOCK_MONOTONIC, ts);
}

const char *rpc_insmod_func(int32_t argc, char **argv, char *buf, size_t bufsize)
{
    const char *path = "mod.so";
    module_error_t err;
    module_loader_t *loader;
    module_init_args_t init_args;

    loader = get_module_loader();
    if (loader == NULL) {
        snprintf(buf, bufsize, "error: module loader not initialized");
        return buf;
    }

    if (argc > 0 && argv[0] != NULL) {
        path = argv[0];
    }

    /* prepare module init args */
    init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
    init_args.log = NULL;
    init_args.get_time = get_time_impl;
    init_args.user_data = NULL;

    err = module_loader_load(loader, path, &init_args);
    if (err == MODULE_ERR_SUCCESS) {
        snprintf(buf, bufsize, "module loaded: %s", path);
    } else {
        snprintf(buf, bufsize, "error: failed to load module: %s (%s)", path,
                module_error_to_string(err));
    }

    return buf;
}

const char *rpc_rmmod_func(int32_t argc, char **argv, char *buf, size_t bufsize)
{
    module_error_t err;
    module_loader_t *loader;

    (void)argc;
    (void)argv;

    loader = get_module_loader();
    if (loader == NULL) {
        snprintf(buf, bufsize, "error: module loader not initialized");
        return buf;
    }

    err = module_loader_unload(loader);
    if (err == MODULE_ERR_SUCCESS) {
        snprintf(buf, bufsize, "module unloaded");
    } else {
        snprintf(buf, bufsize, "error: module not loaded (%s)",
                module_error_to_string(err));
    }

    return buf;
}

