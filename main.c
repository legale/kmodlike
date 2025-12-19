#define _GNU_SOURCE
#include "module_loader.h"
#include "rpc.h"
#include "rpc_commands.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static module_loader_t *g_module_loader = NULL;
static atomic_bool g_sigsegv_received = ATOMIC_VAR_INIT(false);

static void sigsegv_handler(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;

    if (sig == SIGSEGV) {
        if (g_module_loader != NULL &&
                module_loader_get_state(g_module_loader) == MODULE_STATE_LOADED) {
            atomic_store(&g_sigsegv_received, true);
        }
    }
}

static void setup_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        fprintf(stderr, "failed to set sigsegv handler: %s\n", strerror(errno));
        exit(1);
    }
}

static int run_rpc_client(int argc, char **argv)
{
    char response[MAX_PACKET_SIZE];
    const char *server_ip = "127.0.0.1";
    int port = DEFAULT_RPC_PORT;
    int32_t ret;
    char *rpc_argv[MAX_ARGS];
    int32_t rpc_argc = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s insmod <path> | %s rmmod\n", argv[0], argv[0]);
        return 1;
    }

    rpc_argv[rpc_argc++] = argv[1];

    if (strcmp(argv[1], "insmod") == 0) {
        if (argc >= 3) {
            rpc_argv[rpc_argc++] = argv[2];
        } else {
            fprintf(stderr, "usage: %s insmod <path>\n", argv[0]);
            return 1;
        }
    }

    ret = rpc_client_call(server_ip, port, rpc_argc, rpc_argv, response,
            sizeof(response));

    if (ret != RPC_ERR_SUCCESS) {
        fprintf(stderr, "rpc call failed: %d\n", ret);
        return 1;
    }

    printf("%s\n", response);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "insmod") == 0 || strcmp(argv[1], "rmmod") == 0) {
            return run_rpc_client(argc, argv);
        } else {
            fprintf(stderr, "usage: %s [insmod <path>|rmmod]\n", argv[0]);
            fprintf(stderr, "  without arguments: run as daemon with rpc server\n");
            fprintf(stderr, "  insmod <path>: load module via rpc and exit\n");
            fprintf(stderr, "  rmmod: unload module via rpc and exit\n");
            return 1;
        }
    }

    setup_signal_handlers();

    g_module_loader = module_loader_create();
    if (g_module_loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    if (rpc_init() == NULL) {
        fprintf(stderr, "failed to initialize rpc server\n");
        module_loader_destroy(g_module_loader);
        return 1;
    }

    rpc_commands_set_loader(g_module_loader);

    register_str_func("insmod", rpc_insmod_func);
    register_str_func("rmmod", rpc_rmmod_func);
    register_str_func("help", help_func);

    fprintf(stderr, "kmodlike daemon started, rpc server on port %d\n",
            DEFAULT_RPC_PORT);
    fprintf(stderr, "use: ./kmodlike insmod mod.so or ./kmodlike rmmod\n");

    while (1) {
        sleep(1);

        if (atomic_load(&g_sigsegv_received)) {
            fprintf(stderr, "sigsegv received from module, unloading...\n");
            module_loader_unload(g_module_loader);
            fprintf(stderr, "module crashed and was unloaded\n");
            atomic_store(&g_sigsegv_received, false);
            continue;
        }

        if (module_loader_get_state(g_module_loader) == MODULE_STATE_LOADED) {
            if (module_loader_call_hello(g_module_loader) != MODULE_ERR_SUCCESS) {
                fprintf(stderr, "module not_loaded\n");
            }
        } else {
            fprintf(stderr, "module not_loaded\n");
        }
    }

    module_loader_destroy(g_module_loader);
    rpc_deinit();

    return 0;
}
