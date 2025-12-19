#include "../module_loader.h"
#include "../module_interface.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_LOOP_INTERVAL_SEC 1U

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "test failed: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)

typedef struct {
    module_loader_t *loader;
    atomic_bool fatal_signal_received;
} test_context_t;

static test_context_t *g_test_context = NULL;

static void fatal_signal_handler(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;

    if (g_test_context != NULL &&
            (sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE ||
             sig == SIGILL || sig == SIGABRT || sig == SIGSYS)) {
        atomic_store(&g_test_context->fatal_signal_received, true);
    }
}

static const int FATAL_SIGNALS[] = {
    SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS
};

#define FATAL_SIGNALS_COUNT (sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]))

static int setup_signal_handler(test_context_t *ctx)
{
    struct sigaction sa;
    size_t i;

    g_test_context = ctx;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fatal_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < FATAL_SIGNALS_COUNT; i++) {
        if (sigaction(FATAL_SIGNALS[i], &sa, NULL) != 0) {
            return -1;
        }
    }

    return 0;
}

static int get_time_impl(struct timespec *ts)
{
    if (ts == NULL) {
        return -1;
    }
    return clock_gettime(CLOCK_MONOTONIC, ts);
}

static int test_crash_recovery(void)
{
    test_context_t ctx;
    module_error_t err;
    module_state_t state;
    module_init_args_t init_args;
    int i;

    ctx.loader = NULL;
    atomic_store(&ctx.fatal_signal_received, false);

    TEST_ASSERT(setup_signal_handler(&ctx) == 0, "failed to setup signal handler");

    ctx.loader = module_loader_create();
    TEST_ASSERT(ctx.loader != NULL, "module_loader_create failed");

    init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
    init_args.log = NULL;
    init_args.get_time = get_time_impl;
    init_args.user_data = NULL;

    err = module_loader_load(ctx.loader, "tests/fixtures/test_mod_crash.so", &init_args);
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "load should succeed");

    state = module_loader_get_state(ctx.loader);
    TEST_ASSERT(state == MODULE_STATE_LOADED, "state should be LOADED");

    for (i = 0; i < 4; i++) {
        sleep((unsigned int)TEST_LOOP_INTERVAL_SEC);

        if (atomic_load(&ctx.fatal_signal_received)) {
            fprintf(stderr, "fatal signal received, unloading module\n");
            module_loader_unload(ctx.loader);
            atomic_store(&ctx.fatal_signal_received, false);
            break;
        }

        if (module_loader_get_state(ctx.loader) == MODULE_STATE_LOADED) {
            err = module_loader_call_hello(ctx.loader);
            if (err != MODULE_ERR_SUCCESS) {
                fprintf(stderr, "module not_loaded\n");
            }
        } else {
            fprintf(stderr, "module not_loaded\n");
        }
    }

    state = module_loader_get_state(ctx.loader);
    TEST_ASSERT(state == MODULE_STATE_UNLOADED, "module should be unloaded after crash");

    err = module_loader_load(ctx.loader, "tests/fixtures/test_mod_crash.so", &init_args);
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "reload should succeed");

    state = module_loader_get_state(ctx.loader);
    TEST_ASSERT(state == MODULE_STATE_LOADED, "state should be LOADED after reload");

    module_loader_unload(ctx.loader);
    module_loader_destroy(ctx.loader);

    return 0;
}

int main(void)
{
    int ret = 0;

    ret |= test_crash_recovery();

    if (ret == 0) {
        printf("all integration tests passed\n");
    }

    return ret;
}
