# kmodlike

Minimal modular component loading system for dynamic library management with crash recovery.

## Purpose

Provides a reliable module loader that can dynamically load/unload shared libraries, validate module interfaces, and automatically unload modules on crashes (SIGSEGV) without terminating the main program.

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

3. Load module:
```c
module_error_t err = module_loader_load(loader, "path/to/module.so");
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

Modules must implement:
- `int mod_init(void)` - initialization (returns 0 on success)
- `void mod_fini(void)` - cleanup

Optional:
- `void mod_hello(void)` - example function

## License

See LICENSE file.

