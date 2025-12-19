#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static atomic_bool g_module_active = ATOMIC_VAR_INIT(false);
static pthread_t g_thread = 0;

static void *mod_crash_thread(void *arg)
{
    (void)arg;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    sleep(3);

    if (!atomic_load(&g_module_active)) {
        return NULL;
    }

    int *p = NULL;
    *p = 42;
    return NULL;
}

__attribute__((visibility("default")))
int mod_init(void)
{
    atomic_store(&g_module_active, true);
    g_thread = 0;

    if (pthread_create(&g_thread, NULL, mod_crash_thread, NULL) != 0) {
        g_thread = 0;
        return -1;
    }

    return 0;
}

__attribute__((visibility("default")))
void mod_fini(void)
{
    atomic_store(&g_module_active, false);

    if (g_thread != 0) {
        pthread_cancel(g_thread);
        pthread_join(g_thread, NULL);
        g_thread = 0;
    }
}

__attribute__((visibility("default")))
void mod_hello(void)
{
    printf("hello world\n");
    fflush(stdout);
}

