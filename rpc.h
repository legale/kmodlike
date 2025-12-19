#ifndef RPC_H
#define RPC_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L /* For proper POSIX compliance */
#endif

#ifndef __USE_MISC
#define __USE_MISC
#endif


#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>

typedef struct module_loader module_loader_t;

/* Constants */
#define MAX_FUNCTIONS 100
#define MAX_ARGS 10
#define MAX_LINE_LENGTH 256
#define MAX_PACKET_SIZE 4096
#define RPC_SOCKET_PATH_MAX 256

/* Function typedefs */
typedef int32_t (*rpc_cb)(int32_t argc, char **argv, char *buf, size_t bufsize);
typedef const char *(*rpc_string_cb)(int32_t argc, char **argv, char *buf,
                                     size_t bufsize);

/* Error types */
typedef enum {
  RPC_ERR_SUCCESS = 0,
  RPC_ERR_INVALID_PARAM = -1,
  RPC_ERR_BUFFER_OVERFLOW = -2,
  RPC_ERR_NETWORK = -3,
  RPC_ERR_MEMORY = -4,
  RPC_ERR_SYSTEM = -5,
  RPC_ERR_TIMEOUT = -6,
  RPC_ERR_PARSE_ERROR = -7,
  RPC_ERR_NOT_FOUND = -8,
  RPC_ERR_PERMISSION_DENIED = -9,
  RPC_ERR_INVALID_STATE = -10,
  RPC_ERR_SOCKET_ERROR = -11,
  RPC_ERR_MAX_FUNCTIONS_REACHED,
} rpc_error_code_t;

typedef struct {
  char name[50];
  rpc_string_cb func;
} rpc_func_t;

typedef struct {
  rpc_func_t functions[MAX_FUNCTIONS];
  uint32_t function_count;
  int sock_fd;
  atomic_bool keep_running;
  pthread_t server_thread;
  module_loader_t *module_loader;
} rpc_context_t;

/**
 * Get default RPC socket path
 * @param bin_name binary name (e.g., from argv[0])
 * @param path buffer to store path (must be at least RPC_SOCKET_PATH_MAX)
 * @return 0 on success, -1 on error
 */
int rpc_get_default_path(const char *bin_name, char *path, size_t path_size);

/**
 * Initialize the RPC server
 * @param socket_path path to Unix domain socket (NULL for default)
 * @param module_loader module loader instance (can be NULL)
 * @return rpc_context_t * on success, NULL on failure
 */
rpc_context_t *rpc_init(const char *socket_path, module_loader_t *module_loader);

/**
 * Clean up and shut down the RPC server
 */
int rpc_deinit(void);

/**
 * Get current socket path used by RPC server
 * @param path buffer to store path (must be at least RPC_SOCKET_PATH_MAX)
 * @return 0 on success, -1 on error
 */
int rpc_get_socket_path(char *path, size_t path_size);

/**
 * Get module loader from RPC context
 * @return module loader instance or NULL
 */
module_loader_t *rpc_get_module_loader(void);

/**
 * Register a function callback with the RPC server
 *
 * @param name Function name to register
 * @param func Function callback to call when name is invoked
 * @return RPC_ERR_SUCCESS on success, rpc_error_code_t on failure
 */
int32_t rpc_register(const char *name, rpc_cb func);

/**
 * Register a string function callback with the RPC server
 *
 * @param name Function name to register
 * @param func Function callback to call when name is invoked
 * @return RPC_ERR_SUCCESS on success, rpc_error_code_t on failure
 */
int32_t register_str_func(const char *name, rpc_string_cb func);

/* Example default commands */

/**
 * @param argc Argument count
 * @param argv Argument array
 * @return string
 */
const char *echo_func(int32_t argc, char **argv, char *buf, size_t bufsize);
const char *hello_func(int32_t argc, char **argv, char *buf, size_t bufsize);
const char *stop_func(int32_t argc, char **argv, char *buf, size_t bufsize);
const char *help_func(int32_t argc, char **argv, char *buf, size_t bufsize);

/**
 * Send an RPC request to a server and wait for a response
 *
 * @param socket_path path to Unix domain socket
 * @param argc Number of arguments (including function name)
 * @param argv Array of arguments (argv[0] is function name)
 * @param response Buffer to store response
 * @param response_size Size of response buffer
 * @return RPC_ERR_SUCCESS on success, rpc_error_code_t on failure
 */
int32_t rpc_client_call(const char *socket_path, int32_t argc,
                        char **argv, char *response, size_t response_size);

#endif /* RPC_H */