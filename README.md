# kmodlike

Minimal modular component loading system for dynamic library management with crash recovery.

## Purpose

Provides a reliable module loader that can dynamically load/unload shared libraries, validate module interfaces, and automatically unload modules on fatal signals without terminating the main program.

The system handles all fatal signals (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS) by automatically unloading the crashed module and continuing operation.

Remote control is available via RPC over Unix domain socket. Default socket path is `/var/run/<bin_name>.sock` with fallback to `/tmp/<bin_name>.sock` if write access to `/var/run` is not available.

## Build

```bash
make all      # build main program and example module
make lib      # build reusable library (libmodule.so)
make mod      # build example module only
```

## Test

```bash
make test            # run all tests
make test-unit       # run unit tests only
make test-integration # run integration tests
```

## Integration

1. Include the header:
```c
#include "module_loader.h"
```

2. Create loader instance:
```c
module_loader_t *loader = module_loader_create();
```

3. Prepare initialization arguments and load module:
```c
#include "module_interface.h"

module_init_args_t init_args;
init_args.version = MODULE_INIT_ARGS_VERSION_CURRENT;
init_args.log = your_log_function;  // or NULL
init_args.get_time = your_get_time_function;  // or NULL
init_args.user_data = your_user_data;  // or NULL

module_error_t err = module_loader_load(loader, "path/to/module.so", &init_args);
if (err != MODULE_ERR_SUCCESS) {
    // handle error
}
```

4. Use module:
```c
void *symbol;
err = module_loader_get_symbol(loader, "function_name", &symbol);
// call function through symbol
```

5. Unload and cleanup:
```c
module_loader_unload(loader);
module_loader_destroy(loader);
```

## Module Interface

Modules must implement the stable interface defined in `module_interface.h`:

### Required Functions

- `uint32_t module_get_interface_version(void)` - returns interface version (must return `MODULE_INTERFACE_VERSION_CURRENT`)
- `int module_init(const void *init_args)` - initialization (returns 0 on success)
  - `init_args` is a pointer to `module_init_args_t` structure or NULL
  - Structure contains version, logging function, time function, and user data
- `void module_fini(void)` - cleanup

### Optional Functions

- Any module-specific functions can be exported and accessed via `module_loader_get_symbol()`
- Legacy `mod_hello()` function is still supported for backward compatibility

### Interface Versioning

The interface uses versioning to ensure compatibility. Modules must return `MODULE_INTERFACE_VERSION_CURRENT` from `module_get_interface_version()`. The loader will reject modules with incompatible versions.

### Example Module

```c
#include "module_interface.h"
#include <stdint.h>

uint32_t module_get_interface_version(void)
{
    return MODULE_INTERFACE_VERSION_CURRENT;
}

int module_init(const void *init_args)
{
    const module_init_args_t *args = (const module_init_args_t *)init_args;
    if (args != NULL && args->log != NULL) {
        args->log(0, "module initializing");
    }
    return 0;
}

void module_fini(void)
{
    // cleanup
}
```

## License

See LICENSE file.

