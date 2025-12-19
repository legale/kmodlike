#include "module_error.h"

const char *module_error_to_string(module_error_t err)
{
    switch (err) {
    case MODULE_ERR_SUCCESS:
        return "success";
    case MODULE_ERR_INVALID_PARAM:
        return "invalid parameter";
    case MODULE_ERR_ALREADY_LOADED:
        return "module already loaded";
    case MODULE_ERR_NOT_LOADED:
        return "module not loaded";
    case MODULE_ERR_DLOPEN_FAILED:
        return "dlopen failed";
    case MODULE_ERR_MISSING_SYMBOL:
        return "missing required symbol";
    case MODULE_ERR_INIT_FAILED:
        return "module init failed";
    case MODULE_ERR_MEMORY:
        return "memory allocation failed";
    case MODULE_ERR_THREAD:
        return "thread operation failed";
    default:
        return "unknown error";
    }
}

