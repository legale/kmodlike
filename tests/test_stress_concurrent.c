#include "../module_loader.h"
#include "../module_interface.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NUM_THREADS 10
#define ITERATIONS_PER_THREAD 100
#define TEST_MODULE_PATH "tests/fixtures/test_mod_good.so"

typedef struct {
    module_loader_t *loader;
    int thread_id;
    atomic_int *success_count;
    atomic_int *error_count;
} thread_arg_t;

static int get_time_impl(struct timespec *ts)
{
    if (ts == NULL) {
        return -1;
    }
    return clock_gettime(CLOCK_MONOTONIC, ts);
}

static void *stress_load_unload_thread(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    module_loader_t *loader = targ->loader;
    module_init_args_t init_args;
    module_error_t err;
    int i;

    init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
    init_args.log = NULL;
    init_args.get_time = get_time_impl;
    init_args.user_data = NULL;

    for (i = 0; i < ITERATIONS_PER_THREAD; i++) {
        err = module_loader_load(loader, TEST_MODULE_PATH, &init_args);
        if (err == MODULE_ERR_SUCCESS) {
            atomic_fetch_add(targ->success_count, 1);
        } else if (err == MODULE_ERR_ALREADY_LOADED) {
            atomic_fetch_add(targ->success_count, 1);
        } else {
            atomic_fetch_add(targ->error_count, 1);
        }

        usleep((unsigned int)1000);

        err = module_loader_unload(loader);
        if (err == MODULE_ERR_SUCCESS) {
            atomic_fetch_add(targ->success_count, 1);
        } else if (err == MODULE_ERR_NOT_LOADED || err == MODULE_ERR_IN_USE) {
            atomic_fetch_add(targ->success_count, 1);
        } else {
            atomic_fetch_add(targ->error_count, 1);
        }

        usleep((unsigned int)1000);
    }

    return NULL;
}

static void *stress_get_symbol_thread(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    module_loader_t *loader = targ->loader;
    module_error_t err;
    void *symbol;
    int i;

    for (i = 0; i < ITERATIONS_PER_THREAD; i++) {
        err = module_loader_get_symbol(loader, "mod_hello", &symbol);
        if (err == MODULE_ERR_SUCCESS) {
            atomic_fetch_add(targ->success_count, 1);
            module_loader_put_ref(loader);
        } else if (err == MODULE_ERR_NOT_LOADED) {
            atomic_fetch_add(targ->error_count, 1);
        } else {
            atomic_fetch_add(targ->error_count, 1);
        }

        usleep((unsigned int)500);
    }

    return NULL;
}

static void *stress_call_hello_thread(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;
    module_loader_t *loader = targ->loader;
    module_error_t err;
    int i;

    for (i = 0; i < ITERATIONS_PER_THREAD; i++) {
        err = module_loader_call_hello(loader);
        if (err == MODULE_ERR_SUCCESS) {
            atomic_fetch_add(targ->success_count, 1);
        } else if (err == MODULE_ERR_NOT_LOADED) {
            atomic_fetch_add(targ->error_count, 1);
        } else {
            atomic_fetch_add(targ->error_count, 1);
        }

        usleep((unsigned int)500);
    }

    return NULL;
}

static int test_concurrent_load_unload(void)
{
    module_loader_t *loader;
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    atomic_int success_count = ATOMIC_VAR_INIT(0);
    atomic_int error_count = ATOMIC_VAR_INIT(0);
    int i;
    int ret = 0;

    loader = module_loader_create();
    if (loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    for (i = 0; i < NUM_THREADS; i++) {
        args[i].loader = loader;
        args[i].thread_id = i;
        args[i].success_count = &success_count;
        args[i].error_count = &error_count;

        if (pthread_create(&threads[i], NULL, stress_load_unload_thread,
                    &args[i]) != 0) {
            fprintf(stderr, "failed to create thread %d\n", i);
            ret = 1;
            break;
        }
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("concurrent load/unload: success=%d errors=%d\n",
            atomic_load(&success_count), atomic_load(&error_count));

    module_loader_destroy(loader);

    if (atomic_load(&error_count) > NUM_THREADS * ITERATIONS_PER_THREAD / 10) {
        fprintf(stderr, "too many errors in concurrent test\n");
        return 1;
    }

    return ret;
}

static int test_concurrent_get_symbol(void)
{
    module_loader_t *loader;
    module_init_args_t init_args;
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    atomic_int success_count = ATOMIC_VAR_INIT(0);
    atomic_int error_count = ATOMIC_VAR_INIT(0);
    module_error_t err;
    int i;
    int ret = 0;

    loader = module_loader_create();
    if (loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
    init_args.log = NULL;
    init_args.get_time = get_time_impl;
    init_args.user_data = NULL;

    err = module_loader_load(loader, TEST_MODULE_PATH, &init_args);
    if (err != MODULE_ERR_SUCCESS) {
        fprintf(stderr, "failed to load module for test\n");
        module_loader_destroy(loader);
        return 1;
    }

    for (i = 0; i < NUM_THREADS; i++) {
        args[i].loader = loader;
        args[i].thread_id = i;
        args[i].success_count = &success_count;
        args[i].error_count = &error_count;

        if (pthread_create(&threads[i], NULL, stress_get_symbol_thread,
                    &args[i]) != 0) {
            fprintf(stderr, "failed to create thread %d\n", i);
            ret = 1;
            break;
        }
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("concurrent get_symbol: success=%d errors=%d\n",
            atomic_load(&success_count), atomic_load(&error_count));

    module_loader_unload(loader);
    module_loader_destroy(loader);

    if (atomic_load(&error_count) > NUM_THREADS * ITERATIONS_PER_THREAD / 10) {
        fprintf(stderr, "too many errors in concurrent get_symbol test\n");
        return 1;
    }

    return ret;
}

static int test_concurrent_call_hello(void)
{
    module_loader_t *loader;
    module_init_args_t init_args;
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    atomic_int success_count = ATOMIC_VAR_INIT(0);
    atomic_int error_count = ATOMIC_VAR_INIT(0);
    module_error_t err;
    int i;
    int ret = 0;

    loader = module_loader_create();
    if (loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
    init_args.log = NULL;
    init_args.get_time = get_time_impl;
    init_args.user_data = NULL;

    err = module_loader_load(loader, TEST_MODULE_PATH, &init_args);
    if (err != MODULE_ERR_SUCCESS) {
        fprintf(stderr, "failed to load module for test\n");
        module_loader_destroy(loader);
        return 1;
    }

    for (i = 0; i < NUM_THREADS; i++) {
        args[i].loader = loader;
        args[i].thread_id = i;
        args[i].success_count = &success_count;
        args[i].error_count = &error_count;

        if (pthread_create(&threads[i], NULL, stress_call_hello_thread,
                    &args[i]) != 0) {
            fprintf(stderr, "failed to create thread %d\n", i);
            ret = 1;
            break;
        }
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("concurrent call_hello: success=%d errors=%d\n",
            atomic_load(&success_count), atomic_load(&error_count));

    module_loader_unload(loader);
    module_loader_destroy(loader);

    if (atomic_load(&error_count) > NUM_THREADS * ITERATIONS_PER_THREAD / 10) {
        fprintf(stderr, "too many errors in concurrent call_hello test\n");
        return 1;
    }

    return ret;
}


int main(void)
{
    int ret = 0;

    printf("running concurrent stress tests...\n");

    ret |= test_concurrent_load_unload();
    ret |= test_concurrent_get_symbol();
    ret |= test_concurrent_call_hello();

    if (ret == 0) {
        printf("all stress tests passed\n");
    } else {
        printf("some stress tests failed\n");
    }

    return ret;
}

