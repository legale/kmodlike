#ifndef RPC_COMMANDS_H
#define RPC_COMMANDS_H

#include "module_loader.h"

void rpc_commands_set_loader(module_loader_t *loader);

const char *rpc_insmod_func(int32_t argc, char **argv, char *buf, size_t bufsize);

const char *rpc_rmmod_func(int32_t argc, char **argv, char *buf, size_t bufsize);

#endif /* RPC_COMMANDS_H */

