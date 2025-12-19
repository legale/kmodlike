#include "../module_loader.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "test failed: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)

static int test_create_destroy(void)
{
    module_loader_t *loader;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create should return non-NULL");

    module_loader_destroy(loader);

    return 0;
}

static int test_load_unload_good(void)
{
    module_loader_t *loader;
    module_error_t err;
    module_state_t state;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_UNLOADED, "initial state should be UNLOADED");

    err = module_loader_load(loader, "tests/fixtures/test_mod_good.so");
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "load should succeed");

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_LOADED, "state should be LOADED after load");

    err = module_loader_unload(loader);
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "unload should succeed");

    state = module_loader_get_state(loader);
    TEST_ASSERT(state == MODULE_STATE_UNLOADED, "state should be UNLOADED after unload");

    module_loader_destroy(loader);

    return 0;
}

static int test_load_no_init(void)
{
    module_loader_t *loader;
    module_error_t err;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    err = module_loader_load(loader, "tests/fixtures/test_mod_no_init.so");
    TEST_ASSERT(err == MODULE_ERR_MISSING_SYMBOL, "load should fail with MISSING_SYMBOL");

    module_loader_destroy(loader);

    return 0;
}

static int test_load_bad_init(void)
{
    module_loader_t *loader;
    module_error_t err;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    err = module_loader_load(loader, "tests/fixtures/test_mod_bad_init.so");
    TEST_ASSERT(err == MODULE_ERR_INIT_FAILED, "load should fail with INIT_FAILED");

    module_loader_destroy(loader);

    return 0;
}

static int test_get_symbol(void)
{
    module_loader_t *loader;
    module_error_t err;
    void *symbol;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    err = module_loader_load(loader, "tests/fixtures/test_mod_good.so");
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "load should succeed");

    err = module_loader_get_symbol(loader, "mod_hello", &symbol);
    TEST_ASSERT(err == MODULE_ERR_SUCCESS, "get_symbol should succeed");
    TEST_ASSERT(symbol != NULL, "symbol should not be NULL");

    err = module_loader_get_symbol(loader, "nonexistent", &symbol);
    TEST_ASSERT(err == MODULE_ERR_MISSING_SYMBOL, "get_symbol should fail for nonexistent symbol");

    module_loader_destroy(loader);

    return 0;
}

static int test_invalid_params(void)
{
    module_loader_t *loader;
    module_error_t err;

    loader = module_loader_create();
    TEST_ASSERT(loader != NULL, "module_loader_create failed");

    err = module_loader_load(NULL, "test.so");
    TEST_ASSERT(err == MODULE_ERR_INVALID_PARAM, "load with NULL loader should fail");

    err = module_loader_load(loader, NULL);
    TEST_ASSERT(err == MODULE_ERR_INVALID_PARAM, "load with NULL path should fail");

    err = module_loader_unload(NULL);
    TEST_ASSERT(err == MODULE_ERR_INVALID_PARAM, "unload with NULL loader should fail");

    module_loader_destroy(loader);

    return 0;
}

int main(void)
{
    int ret = 0;

    ret |= test_create_destroy();
    ret |= test_load_unload_good();
    ret |= test_load_no_init();
    ret |= test_load_bad_init();
    ret |= test_get_symbol();
    ret |= test_invalid_params();

    if (ret == 0) {
        printf("all tests passed\n");
    }

    return ret;
}

