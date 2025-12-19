#include "../module_loader.h"
#include "../module_interface.h"
#include "../rpc.h"
#include "../rpc_commands.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NUM_CLIENTS 20
#define ITERATIONS_PER_CLIENT 50

static int rpc_client_worker(const char *socket_path, int client_id)
{
    char response[4096];
    int32_t rpc_ret;
    char *argv_insmod[] = {"insmod", "tests/fixtures/test_mod_good.so"};
    char *argv_rmmod[] = {"rmmod"};
    int i;
    int success = 0;
    int errors = 0;

    for (i = 0; i < ITERATIONS_PER_CLIENT; i++) {
        rpc_ret = rpc_client_call(socket_path, 2, argv_insmod, response,
                sizeof(response));
        if (rpc_ret == RPC_ERR_SUCCESS) {
            success++;
        } else {
            errors++;
        }

        usleep((unsigned int)(10000 + (client_id * 1000)));

        rpc_ret = rpc_client_call(socket_path, 1, argv_rmmod, response,
                sizeof(response));
        if (rpc_ret == RPC_ERR_SUCCESS) {
            success++;
        } else {
            errors++;
        }

        usleep((unsigned int)(10000 + (client_id * 1000)));
    }

    return (errors < ITERATIONS_PER_CLIENT) ? 0 : 1;
}

static int test_rpc_stress_with_server(void)
{
    module_loader_t *loader;
    rpc_context_t *rpc_ctx;
    pid_t client_pids[NUM_CLIENTS];
    char socket_path[256];
    int i;
    int status;
    int success_count = 0;
    int ret = 0;

    loader = module_loader_create();
    if (loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    snprintf(socket_path, sizeof(socket_path), "/tmp/kmodlike_stress_test.sock");

    rpc_ctx = rpc_init(socket_path, loader);
    if (rpc_ctx == NULL) {
        fprintf(stderr, "failed to initialize rpc server\n");
        module_loader_destroy(loader);
        return 1;
    }

    rpc_commands_set_loader(loader);
    register_str_func("insmod", rpc_insmod_func);
    register_str_func("rmmod", rpc_rmmod_func);

    for (i = 0; i < NUM_CLIENTS; i++) {
        client_pids[i] = fork();
        if (client_pids[i] == 0) {
            exit(rpc_client_worker(socket_path, i));
        } else if (client_pids[i] < 0) {
            fprintf(stderr, "fork failed for client %d\n", i);
            ret = 1;
            break;
        }
    }

    for (i = 0; i < NUM_CLIENTS; i++) {
        if (client_pids[i] > 0) {
            waitpid(client_pids[i], &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                success_count++;
            }
        }
    }

    rpc_deinit();
    module_loader_destroy(loader);

    printf("rpc stress test: %d/%d clients succeeded\n", success_count, NUM_CLIENTS);

    if (success_count < NUM_CLIENTS / 2) {
        fprintf(stderr, "too few successful rpc clients\n");
        return 1;
    }

    return ret;
}

int main(void)
{
    int ret = 0;

    printf("running rpc stress test with concurrent clients...\n");

    ret = test_rpc_stress_with_server();

    if (ret == 0) {
        printf("rpc stress test passed\n");
    } else {
        printf("rpc stress test failed\n");
    }

    return ret;
}

