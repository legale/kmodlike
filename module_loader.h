#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include "module_error.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MODULE_STATE_UNLOADED = 0,
    MODULE_STATE_LOADED = 1
} module_state_t;

struct module_loader;

typedef struct module_loader module_loader_t;

/**
 * create module loader instance
 * @return pointer to module loader or NULL on error
 */
module_loader_t *module_loader_create(void);

/**
 * destroy module loader instance
 * automatically unloads module if loaded
 * @param loader module loader instance or NULL
 */
void module_loader_destroy(module_loader_t *loader);

/**
 * load module from path
 * validates module interface and calls mod_init
 * @param loader module loader instance
 * @param path path to module shared library
 * @return error code
 */
module_error_t module_loader_load(module_loader_t *loader, const char *path);

/**
 * unload module
 * calls mod_fini before unloading
 * @param loader module loader instance
 * @return error code
 */
module_error_t module_loader_unload(module_loader_t *loader);

/**
 * get current module state
 * @param loader module loader instance
 * @return module state
 */
module_state_t module_loader_get_state(const module_loader_t *loader);

/**
 * get symbol from loaded module
 * @param loader module loader instance
 * @param name symbol name
 * @param symbol output pointer for symbol address
 * @return error code
 */
module_error_t module_loader_get_symbol(module_loader_t *loader,
        const char *name, void **symbol);

/**
 * get last error code
 * @param loader module loader instance
 * @return error code
 */
module_error_t module_loader_get_error(const module_loader_t *loader);

/**
 * call mod_hello function from loaded module
 * @param loader module loader instance
 * @return error code
 */
module_error_t module_loader_call_hello(module_loader_t *loader);

#endif /* MODULE_LOADER_H */
