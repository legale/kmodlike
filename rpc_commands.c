#include "module_loader.h"
#include "rpc_commands.h"
#include "rpc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static module_loader_t *g_module_loader = NULL;

void rpc_commands_set_loader(module_loader_t *loader)
{
    g_module_loader = loader;
}

const char *rpc_insmod_func(int32_t argc, char **argv, char *buf, size_t bufsize)
{
    const char *path = "mod.so";
    module_error_t err;

    if (g_module_loader == NULL) {
        snprintf(buf, bufsize, "error: module loader not initialized");
        return buf;
    }

    if (argc > 0 && argv[0] != NULL) {
        path = argv[0];
    }

    err = module_loader_load(g_module_loader, path);
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

    (void)argc;
    (void)argv;

    if (g_module_loader == NULL) {
        snprintf(buf, bufsize, "error: module loader not initialized");
        return buf;
    }

    err = module_loader_unload(g_module_loader);
    if (err == MODULE_ERR_SUCCESS) {
        snprintf(buf, bufsize, "module unloaded");
    } else {
        snprintf(buf, bufsize, "error: module not loaded (%s)",
                module_error_to_string(err));
    }

    return buf;
}

