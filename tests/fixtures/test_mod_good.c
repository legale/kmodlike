#define _GNU_SOURCE
#include <stdio.h>

__attribute__((visibility("default")))
int mod_init(void)
{
    return 0;
}

__attribute__((visibility("default")))
void mod_fini(void)
{
}

__attribute__((visibility("default")))
void mod_hello(void)
{
    printf("hello from test module\n");
}

