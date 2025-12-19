#include "../../module_interface.h"
#include <stdint.h>
#include <stdio.h>

__attribute__((visibility("default")))
uint32_t module_get_interface_version(void)
{
    return MODULE_INTERFACE_VERSION_CURRENT;
}

__attribute__((visibility("default")))
int module_init(const void *init_args)
{
    (void)init_args;
    return 0;
}

__attribute__((visibility("default")))
void module_fini(void)
{
}

__attribute__((visibility("default")))
void mod_hello(void)
{
    printf("hello from test module\n");
}

