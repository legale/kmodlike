#include "module_loader.h"
#include "module_interface.h"
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

#define DAEMON_LOOP_INTERVAL_SEC 1U

typedef struct {
    module_loader_t *module_loader;
    atomic_bool fatal_signal_received;
} app_context_t;

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    case SIGSYS:
        return "SIGSYS";
    default:
        return "UNKNOWN";
    }
}

static app_context_t *g_app_context = NULL;

static void fatal_signal_handler(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;

    if (g_app_context != NULL && g_app_context->module_loader != NULL &&
            module_loader_get_state(g_app_context->module_loader) == MODULE_STATE_LOADED) {
        fprintf(stderr, "fatal signal %s received from module\n", signal_name(sig));
        atomic_store(&g_app_context->fatal_signal_received, true);
    }
}

static const int FATAL_SIGNALS[] = {
    SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS
};

#define FATAL_SIGNALS_COUNT (sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]))

static void setup_signal_handlers(app_context_t *ctx)
{
    struct sigaction sa;
    size_t i;

    g_app_context = ctx;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fatal_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < FATAL_SIGNALS_COUNT; i++) {
        if (sigaction(FATAL_SIGNALS[i], &sa, NULL) != 0) {
            fprintf(stderr, "failed to set handler for signal %d: %s\n",
                    FATAL_SIGNALS[i], strerror(errno));
            exit(1);
        }
    }
}

static int run_rpc_client(int argc, char **argv)
{
    char response[MAX_PACKET_SIZE];
    char socket_path[RPC_SOCKET_PATH_MAX];
    char tmp_path[RPC_SOCKET_PATH_MAX];
    int32_t ret;
    char *rpc_argv[MAX_ARGS];
    int32_t rpc_argc = 0;
    const char *base;

    if (argc < 2) {
        fprintf(stderr, "usage: %s insmod <path> | %s rmmod\n", argv[0], argv[0]);
        return 1;
    }

    if (rpc_get_default_path(argv[0], socket_path, sizeof(socket_path)) != 0) {
        fprintf(stderr, "failed to get socket path\n");
        return 1;
    }

    /* Prepare /tmp fallback path */
    base = strrchr(argv[0], '/');
    base = (base != NULL) ? base + 1 : argv[0];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s.sock", base);

    rpc_argv[rpc_argc++] = argv[1];

    if (strcmp(argv[1], "insmod") == 0) {
        if (argc >= 3) {
            rpc_argv[rpc_argc++] = argv[2];
        } else {
            fprintf(stderr, "usage: %s insmod <path>\n", argv[0]);
            return 1;
        }
    }

    /* Try /var/run first, then /tmp fallback */
    ret = rpc_client_call(socket_path, rpc_argc, rpc_argv, response,
            sizeof(response));
    if (ret != RPC_ERR_SUCCESS) {
        /* Try /tmp fallback */
        ret = rpc_client_call(tmp_path, rpc_argc, rpc_argv, response,
                sizeof(response));
    }

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

    app_context_t ctx;

    ctx.module_loader = NULL;
    atomic_store(&ctx.fatal_signal_received, false);

    setup_signal_handlers(&ctx);

    ctx.module_loader = module_loader_create();
    if (ctx.module_loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    if (rpc_init(NULL, ctx.module_loader) == NULL) {
        fprintf(stderr, "failed to initialize rpc server\n");
        module_loader_destroy(ctx.module_loader);
        return 1;
    }

    rpc_commands_set_loader(ctx.module_loader);

    register_str_func("insmod", rpc_insmod_func);
    register_str_func("rmmod", rpc_rmmod_func);
    register_str_func("help", help_func);

    {
        char socket_path[RPC_SOCKET_PATH_MAX];
        if (rpc_get_socket_path(socket_path, sizeof(socket_path)) == 0) {
            fprintf(stderr, "kmodlike daemon started, rpc server on socket %s\n",
                    socket_path);
        } else {
            fprintf(stderr, "kmodlike daemon started\n");
        }
    }
    fprintf(stderr, "use: ./kmodlike insmod mod.so or ./kmodlike rmmod\n");

    while (1) {
        sleep((unsigned int)DAEMON_LOOP_INTERVAL_SEC);

        if (atomic_load(&ctx.fatal_signal_received)) {
            fprintf(stderr, "fatal signal received from module, unloading...\n");
            module_loader_unload(ctx.module_loader);
            fprintf(stderr, "module crashed and was unloaded\n");
            atomic_store(&ctx.fatal_signal_received, false);
            continue;
        }

        if (module_loader_get_state(ctx.module_loader) == MODULE_STATE_LOADED) {
            if (module_loader_call_hello(ctx.module_loader) != MODULE_ERR_SUCCESS) {
                fprintf(stderr, "module not_loaded\n");
            }
        } else {
            fprintf(stderr, "module not_loaded\n");
        }
    }

    module_loader_destroy(ctx.module_loader);
    rpc_deinit();

    return 0;
}
