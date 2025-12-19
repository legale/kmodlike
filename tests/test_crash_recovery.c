#include "../module_loader.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "test failed: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)

static atomic_bool g_fatal_signal_received = ATOMIC_VAR_INIT(false);

static void fatal_signal_handler(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;

    if (sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE ||
            sig == SIGILL || sig == SIGABRT || sig == SIGSYS) {
        atomic_store(&g_fatal_signal_received, true);
    }
}

static int setup_signal_handler(void)
{
    struct sigaction sa;

    int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS};
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fatal_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (sigaction(signals[i], &sa, NULL) != 0) {
            return -1;
        }
    }

    return 0;
}

static int test_crash_recovery(void)
{
    module_loader_t *loader;
    module_error_t err;
    module_state_t state;
    int i;

    TEST_ASSERT(setup_signal_handler() == 0, "failed to setup signal handler");

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    err = module_loader_load(loader, "tests/fixtures/test_mod_crash.so");
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "load should succeed");

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_LOADED, "state should be LOADED");

    for (i = 0; i < 4; i++) {
        sleep(1);

        if (atomic_load(&g_fatal_signal_received)) {
            fprintf(stderr, "fatal signal received, unloading module\n");
            module_loader_unload(loader);
            atomic_store(&g_fatal_signal_received, false);
            break;
        }

        if (module_loader_get_state(loader) == MODULE_STATE_LOADED) {
            err = module_loader_call_hello(loader);
            if (err != MODULE_ERR_SUCCESS) {
                fprintf(stderr, "module not_loaded\n");
            }
        } else {
            fprintf(stderr, "module not_loaded\n");
        }
    }

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_UNLOADED, "module should be unloaded after crash");

    err = module_loader_load(loader, "tests/fixtures/test_mod_crash.so");
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "reload should succeed");

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_LOADED, "state should be LOADED after reload");

    module_loader_unload(loader);
    module_loader_destroy(loader);

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

