#include "mod.h"
#include "module_interface.h"
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MODULE_CRASH_DELAY_SEC 3U

/* флаг активности модуля - используется для предотвращения фатальных сигналов из выгруженных модулей */
static atomic_bool g_module_active = ATOMIC_VAR_INIT(false);
/* ID потока для возможности его завершения */
static pthread_t g_thread = 0;

/* Функция модуля, печатающая hello world */
__attribute__((visibility("default")))
void mod_hello(void) {
    printf("hello world\n");
    fflush(stdout);
}

/* поток, который через 3 секунды вызывает SIGSEGV */
static void *mod_crash_thread(void *arg) {
    (void)arg;
    
    /* Делаем поток отменяемым в любой момент */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    sleep((unsigned int)MODULE_CRASH_DELAY_SEC);
    
    /* Проверяем, что модуль все еще активен */
    if (!atomic_load(&g_module_active)) {
        return NULL; /* Модуль был выгружен, не вызываем SIGSEGV */
    }
    
    /* намеренно вызываем SIGSEGV через разыменование NULL */
    int *p = NULL;
    *p = 42;
    return NULL;
}

/* get module interface version */
__attribute__((visibility("default")))
uint32_t module_get_interface_version(void)
{
    return MODULE_INTERFACE_VERSION_CURRENT;
}

/* module initialization function */
__attribute__((visibility("default")))
int module_init(const void *init_args)
{
    const module_init_args_t *args;

    /* устанавливаем флаг активности и создаем поток */
    atomic_store(&g_module_active, true);
    g_thread = 0;

    /* если переданы аргументы, можно использовать функции логирования */
    args = (const module_init_args_t *)init_args;
    if (args != NULL && args->version == MODULE_INIT_ARGS_VERSION_CURRENT) {
        if (args->log != NULL) {
            args->log(0, "module initializing");
        }
    }

    /* создаем joinable поток (не detached!), чтобы можно было его завершить */
    if (pthread_create(&g_thread, NULL, mod_crash_thread, NULL) != 0) {
        g_thread = 0;
        if (args != NULL && args->log != NULL) {
            args->log(1, "failed to create thread");
        }
        return -1; /* ошибка создания потока */
    }

    if (args != NULL && args->log != NULL) {
        args->log(0, "module initialized");
    }

    return 0; /* успешная инициализация */
}

/* module finalization function */
__attribute__((visibility("default")))
void module_fini(void)
{
    /* сбрасываем флаг активности, чтобы поток не вызывал фатальные сигналы */
    atomic_store(&g_module_active, false);

    /* явно завершаем поток, если он еще работает */
    if (g_thread != 0) {
        pthread_cancel(g_thread);
        /* ждем завершения потока - модуль не выгрузится пока поток не завершится */
        pthread_join(g_thread, NULL);
        g_thread = 0;
    }
}

