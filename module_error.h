#ifndef MODULE_ERROR_H
#define MODULE_ERROR_H

#include <stdint.h>

typedef enum {
    MODULE_ERR_SUCCESS = 0,
    MODULE_ERR_INVALID_PARAM = -1,
    MODULE_ERR_ALREADY_LOADED = -2,
    MODULE_ERR_NOT_LOADED = -3,
    MODULE_ERR_DLOPEN_FAILED = -4,
    MODULE_ERR_MISSING_SYMBOL = -5,
    MODULE_ERR_INIT_FAILED = -6,
    MODULE_ERR_MEMORY = -7,
    MODULE_ERR_THREAD = -8
} module_error_t;

const char *module_error_to_string(module_error_t err);

#endif /* MODULE_ERROR_H */

