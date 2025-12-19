#include "module_loader.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODULE_PATH_MAX 256U
#define MODULE_PATH_MIN 1U

struct module_loader {
    void *handle;
    uint32_t (*get_version_func)(void);
    int (*init_func)(const void *);
    void (*fini_func)(void);
    void (*hello_func)(void);
    uint32_t interface_version;
    pthread_mutex_t mutex;
    atomic_bool loaded;
    atomic_int ref_count;
    char path[MODULE_PATH_MAX];
    module_error_t last_error;
};

module_loader_t *module_loader_create(void)
{
    module_loader_t *loader;

    loader = calloc(1U, sizeof(*loader));
    if (loader == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&loader->mutex, NULL) != 0) {
        free(loader);
        return NULL;
    }

    loader->loaded = ATOMIC_VAR_INIT(false);
    loader->ref_count = ATOMIC_VAR_INIT(0);
    loader->last_error = MODULE_ERR_SUCCESS;
    loader->interface_version = 0U;
    loader->get_version_func = NULL;
    loader->init_func = NULL;
    loader->fini_func = NULL;
    loader->hello_func = NULL;

    return loader;
}

void module_loader_destroy(module_loader_t *loader)
{
    if (loader == NULL) {
        return;
    }

    if (atomic_load(&loader->loaded)) {
        module_loader_unload(loader);
    }

    pthread_mutex_destroy(&loader->mutex);
    free(loader);
}

module_error_t module_loader_load(module_loader_t *loader, const char *path,
        const module_init_args_t *init_args)
{
    module_error_t ret;
    size_t path_len;
    void *handle;
    uint32_t module_version;

    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    if (path == NULL) {
        loader->last_error = MODULE_ERR_INVALID_PARAM;
        return MODULE_ERR_INVALID_PARAM;
    }

    path_len = strlen(path);
    if (path_len < MODULE_PATH_MIN || path_len >= MODULE_PATH_MAX) {
        loader->last_error = MODULE_ERR_INVALID_PARAM;
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (loader->handle != NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_ALREADY_LOADED;
        return MODULE_ERR_ALREADY_LOADED;
    }

    handle = dlopen(path, RTLD_LAZY);
    if (handle == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_DLOPEN_FAILED;
        return MODULE_ERR_DLOPEN_FAILED;
    }

    loader->get_version_func = (uint32_t (*)(void))dlsym(handle,
            "module_get_interface_version");
    if (loader->get_version_func == NULL) {
        dlclose(handle);
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    module_version = loader->get_version_func();
    if (module_version != MODULE_INTERFACE_VERSION_CURRENT) {
        dlclose(handle);
        loader->get_version_func = NULL;
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_VERSION_MISMATCH;
        return MODULE_ERR_VERSION_MISMATCH;
    }

    loader->init_func = (int (*)(const void *))dlsym(handle, "module_init");
    if (loader->init_func == NULL) {
        dlclose(handle);
        loader->get_version_func = NULL;
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    loader->fini_func = (void (*)(void))dlsym(handle, "module_fini");
    if (loader->fini_func == NULL) {
        dlclose(handle);
        loader->get_version_func = NULL;
        loader->init_func = NULL;
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    loader->hello_func = (void (*)(void))dlsym(handle, "mod_hello");

    ret = loader->init_func(init_args);
    if (ret != 0) {
        dlclose(handle);
        loader->handle = NULL;
        loader->get_version_func = NULL;
        loader->init_func = NULL;
        loader->fini_func = NULL;
        loader->hello_func = NULL;
        loader->interface_version = 0U;
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_INIT_FAILED;
        return MODULE_ERR_INIT_FAILED;
    }

    loader->handle = handle;
    loader->interface_version = module_version;
    strncpy(loader->path, path, MODULE_PATH_MAX - 1U);
    loader->path[MODULE_PATH_MAX - 1U] = '\0';
    atomic_store(&loader->loaded, true);
    atomic_store(&loader->ref_count, 0);
    loader->last_error = MODULE_ERR_SUCCESS;

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_unload(module_loader_t *loader)
{
    int ref_count;

    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (loader->handle == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    ref_count = atomic_load(&loader->ref_count);
    if (ref_count > 0) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_IN_USE;
        return MODULE_ERR_IN_USE;
    }

    if (loader->fini_func != NULL) {
        loader->fini_func();
    }

    dlclose(loader->handle);
    loader->handle = NULL;
    loader->get_version_func = NULL;
    loader->init_func = NULL;
    loader->fini_func = NULL;
    loader->hello_func = NULL;
    loader->interface_version = 0U;
    atomic_store(&loader->loaded, false);
    atomic_store(&loader->ref_count, 0);
    loader->last_error = MODULE_ERR_SUCCESS;

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

module_state_t module_loader_get_state(const module_loader_t *loader)
{
    if (loader == NULL) {
        return MODULE_STATE_UNLOADED;
    }

    return atomic_load(&loader->loaded) ? MODULE_STATE_LOADED :
           MODULE_STATE_UNLOADED;
}

module_error_t module_loader_get_symbol(module_loader_t *loader,
        const char *name, void **symbol)
{
    module_error_t ret;

    if (loader == NULL || name == NULL || symbol == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    ret = module_loader_get_ref(loader);
    if (ret != MODULE_ERR_SUCCESS) {
        return ret;
    }

    pthread_mutex_lock(&loader->mutex);

    if (loader->handle == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        module_loader_put_ref(loader);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    *symbol = dlsym(loader->handle, name);
    if (*symbol == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        module_loader_put_ref(loader);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_get_error(const module_loader_t *loader)
{
    module_error_t err;

    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock((pthread_mutex_t *)&loader->mutex);
    err = loader->last_error;
    pthread_mutex_unlock((pthread_mutex_t *)&loader->mutex);

    return err;
}

module_error_t module_loader_call_hello(module_loader_t *loader)
{
    module_error_t ret;

    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    ret = module_loader_get_ref(loader);
    if (ret != MODULE_ERR_SUCCESS) {
        return ret;
    }

    pthread_mutex_lock(&loader->mutex);

    if (!atomic_load(&loader->loaded) || loader->hello_func == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        module_loader_put_ref(loader);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    pthread_mutex_unlock(&loader->mutex);

    loader->hello_func();

    module_loader_put_ref(loader);
    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_get_ref(module_loader_t *loader)
{
    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (!atomic_load(&loader->loaded)) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    atomic_fetch_add(&loader->ref_count, 1);
    pthread_mutex_unlock(&loader->mutex);

    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_put_ref(module_loader_t *loader)
{
    int ref_count;

    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    ref_count = atomic_fetch_sub(&loader->ref_count, 1);
    if (ref_count <= 0) {
        atomic_store(&loader->ref_count, 0);
        return MODULE_ERR_INVALID_PARAM;
    }

    return MODULE_ERR_SUCCESS;
}

