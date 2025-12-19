#define _GNU_SOURCE
#include "mod.h"
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
    
    sleep(3);
    
    /* Проверяем, что модуль все еще активен */
    if (!atomic_load(&g_module_active)) {
        return NULL; /* Модуль был выгружен, не вызываем SIGSEGV */
    }
    
    /* намеренно вызываем SIGSEGV через разыменование NULL */
    int *p = NULL;
    *p = 42;
    return NULL;
}

/* Функция инициализации модуля (обязательная, вызывается явно основной программой) */
__attribute__((visibility("default")))
int mod_init(void) {
    /* Устанавливаем флаг активности и создаем поток */
    atomic_store(&g_module_active, true);
    g_thread = 0;
    
    /* Создаем joinable поток (НЕ detached!), чтобы можно было его завершить */
    if (pthread_create(&g_thread, NULL, mod_crash_thread, NULL) != 0) {
        g_thread = 0;
        return -1; /* Ошибка создания потока */
    }
    
    return 0; /* Успешная инициализация */
}

/* Функция деинициализации модуля (обязательная, вызывается явно основной программой) */
__attribute__((visibility("default")))
void mod_fini(void) {
    /* сбрасываем флаг активности, чтобы поток не вызывал фатальные сигналы */
    atomic_store(&g_module_active, false);
    
    /* Явно завершаем поток, если он еще работает */
    if (g_thread != 0) {
        pthread_cancel(g_thread);
        /* Ждем завершения потока - модуль не выгрузится пока поток не завершится */
        pthread_join(g_thread, NULL);
        g_thread = 0;
    }
}

