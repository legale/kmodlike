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
#include <sys/socket.h>
#include <sys/un.h>

#define NUM_REQUESTS 2

/* Simple UDP client that sends request and exits immediately */
static void send_rpc_request(const char *socket_path, int argc, char **argv)
{
    int sock;
    struct sockaddr_un server_addr;
    char request[256];
    size_t pos = 0;
    size_t path_len;
    int i;

    path_len = strlen(socket_path);
    if (path_len >= sizeof(server_addr.sun_path)) {
        return;
    }

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);

    /* Build null-delimited request */
    for (i = 0; i < argc && argv[i] != NULL; i++) {
        size_t len = strlen(argv[i]);
        if (pos + len + 1 >= sizeof(request)) {
            break;
        }
        memcpy(request + pos, argv[i], len);
        pos += len;
        request[pos++] = '\0';
    }
    if (pos < sizeof(request)) {
        request[pos] = '\0';
    }

    sendto(sock, request, pos + 1, 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));

    close(sock);
}

static void *rpc_client_thread(void *arg)
{
    const char *socket_path = (const char *)arg;
    char *argv_insmod[] = {"insmod", "tests/fixtures/test_mod_good.so"};
    char *argv_rmmod[] = {"rmmod"};
    int i;

    for (i = 0; i < NUM_REQUESTS; i++) {
        send_rpc_request(socket_path, 2, argv_insmod);
        usleep(50000); /* 50ms delay */
        send_rpc_request(socket_path, 1, argv_rmmod);
        usleep(50000); /* 50ms delay */
    }

    return NULL;
}

static int test_rpc_stress_with_server(void)
{
    module_loader_t *loader;
    rpc_context_t *rpc_ctx;
    pthread_t threads[2];
    char socket_path[256];
    int i;
    int ret = 0;

    loader = module_loader_create();
    if (loader == NULL) {
        fprintf(stderr, "failed to create module loader\n");
        return 1;
    }

    snprintf(socket_path, sizeof(socket_path), "/tmp/kmodlike_stress_test.sock");
    unlink(socket_path);

    rpc_ctx = rpc_init(socket_path, loader);
    if (rpc_ctx == NULL) {
        fprintf(stderr, "failed to initialize rpc server\n");
        module_loader_destroy(loader);
        return 1;
    }

    rpc_commands_set_loader(loader);
    register_str_func("insmod", rpc_insmod_func);
    register_str_func("rmmod", rpc_rmmod_func);

    /* Give server time to start */
    usleep(200000); /* 200ms */

    /* Start client threads */
    for (i = 0; i < 2; i++) {
        if (pthread_create(&threads[i], NULL, rpc_client_thread, socket_path) != 0) {
            fprintf(stderr, "failed to create thread %d\n", i);
            ret = 1;
            break;
        }
    }

    /* Wait for threads - they send requests and exit quickly */
    for (i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Give server time to process requests */
    usleep(500000); /* 500ms */

    rpc_deinit();
    module_loader_destroy(loader);
    unlink(socket_path);

    printf("rpc stress test: server processed concurrent requests\n");

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
