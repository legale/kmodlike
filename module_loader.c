#define _GNU_SOURCE
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
    int (*init_func)(void);
    void (*fini_func)(void);
    void (*hello_func)(void);
    pthread_mutex_t mutex;
    atomic_bool loaded;
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
    loader->last_error = MODULE_ERR_SUCCESS;

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

module_error_t module_loader_load(module_loader_t *loader, const char *path)
{
    module_error_t ret;
    size_t path_len;
    void *handle;

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

    loader->init_func = (int (*)(void))dlsym(handle, "mod_init");
    if (loader->init_func == NULL) {
        dlclose(handle);
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    loader->fini_func = (void (*)(void))dlsym(handle, "mod_fini");
    if (loader->fini_func == NULL) {
        dlclose(handle);
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    loader->hello_func = (void (*)(void))dlsym(handle, "mod_hello");

    ret = loader->init_func();
    if (ret != 0) {
        dlclose(handle);
        loader->handle = NULL;
        loader->init_func = NULL;
        loader->fini_func = NULL;
        loader->hello_func = NULL;
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_INIT_FAILED;
        return MODULE_ERR_INIT_FAILED;
    }

    loader->handle = handle;
    strncpy(loader->path, path, MODULE_PATH_MAX - 1U);
    loader->path[MODULE_PATH_MAX - 1U] = '\0';
    atomic_store(&loader->loaded, true);
    loader->last_error = MODULE_ERR_SUCCESS;

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_unload(module_loader_t *loader)
{
    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (loader->handle == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    if (loader->fini_func != NULL) {
        loader->fini_func();
    }

    dlclose(loader->handle);
    loader->handle = NULL;
    loader->init_func = NULL;
    loader->fini_func = NULL;
    loader->hello_func = NULL;
    atomic_store(&loader->loaded, false);
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
    if (loader == NULL || name == NULL || symbol == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (loader->handle == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    *symbol = dlsym(loader->handle, name);
    if (*symbol == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_MISSING_SYMBOL;
        return MODULE_ERR_MISSING_SYMBOL;
    }

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

module_error_t module_loader_get_error(const module_loader_t *loader)
{
    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    return loader->last_error;
}

module_error_t module_loader_call_hello(module_loader_t *loader)
{
    if (loader == NULL) {
        return MODULE_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&loader->mutex);

    if (!atomic_load(&loader->loaded) || loader->hello_func == NULL) {
        pthread_mutex_unlock(&loader->mutex);
        loader->last_error = MODULE_ERR_NOT_LOADED;
        return MODULE_ERR_NOT_LOADED;
    }

    loader->hello_func();

    pthread_mutex_unlock(&loader->mutex);
    return MODULE_ERR_SUCCESS;
}

