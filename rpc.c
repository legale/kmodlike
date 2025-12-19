#include "rpc.h"
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

// constants
#define RPC_MAX_PACKET_SIZE 4096
#define RPC_DEFAULT_TIMEOUT_SEC 5

/* Structure to track client requests */
typedef struct {
  struct sockaddr_un addr;
  socklen_t addr_len;
} client_info_t;

/* Global context */
static rpc_context_t g_ctx = {0};
static char g_socket_path[RPC_SOCKET_PATH_MAX] = {0};

/* Simple logging macro */
#define RPC_LOG(fmt, ...)                                                      \
  fprintf(stderr, "%s: " fmt "\n", __func__, ##__VA_ARGS__)

/* Function implementations */
const char *help_func(int32_t argc, char **argv, char *buf, size_t bufsize) {
  (void)argc;
  (void)argv;

  size_t left = bufsize;
  size_t written = 0;
  char *p = buf;

  buf[0] = '\0';

  for (uint32_t i = 0; i < g_ctx.function_count; i++) {
    if (g_ctx.functions[i].name[0] == '\0')
      continue;

    written = snprintf(p, left, "%s\n", g_ctx.functions[i].name);
    if (written >= left)
      break;
    left -= written;
    p += written;
  }

  return buf;
}

const char *hello_func(int32_t argc, char **argv, char *buf, size_t bufsize) {
  RPC_LOG("argc=%d", argc);
  size_t pos = 0;
  int32_t i;
  int32_t len;
  size_t remaining = bufsize;

  /* Basic message */
  remaining = bufsize - pos;
  if (remaining <= 1) {
    return "error: buffer overflow";
  }

  len = snprintf(buf + pos, remaining, "world");
  if (len < 0 || (size_t)len >= remaining) {
    return "error: buffer overflow";
  }
  pos += (size_t)len;

  /* Add arguments if present */
  if (argc > 0 && argv != NULL) {
    remaining = sizeof(buf) - pos;
    if (remaining <= 1) {
      return buf; /* Return what we have so far */
    }

    len = snprintf(buf + pos, remaining, " (argc=%d", argc);
    if (len < 0 || (size_t)len >= remaining) {
      return buf; /* Return what we have so far */
    }
    pos += (size_t)len;

    /* Add each argument */
    for (i = 0; i < argc; i++) {
      if (argv[i] == NULL) {
        continue;
      }

      remaining = sizeof(buf) - pos;
      if (remaining <= 1) {
        break;
      }

      len = snprintf(buf + pos, remaining, " argv[%d]='%s'", i, argv[i]);
      if (len < 0 || (size_t)len >= remaining) {
        break;
      }
      pos += (size_t)len;
    }

    /* Close parenthesis */
    remaining = sizeof(buf) - pos;
    if (remaining > 1) {
      len = snprintf(buf + pos, remaining, ")");
      if (len > 0 && (size_t)len < remaining) {
        pos += (size_t)len;
      }
    }
  }

  return buf;
}

const char *stop_func(int32_t argc, char **argv, char *buf, size_t bufsize) {
  /* Intentionally unused parameters - documented */
  (void)argc;
  (void)argv;
  atomic_store(&g_ctx.keep_running, false);
  return "0";
}

const char *echo_func(int32_t argc, char **argv, char *buf, size_t bufsize) {
  size_t pos = 0;
  int32_t i;
  int32_t len;
  size_t remaining;

  if (argv == NULL) {
    return "error: invalid arguments";
  }

  /* Add argument count */
  remaining = bufsize - pos;
  if (remaining <= 1) {
    return "error: buffer overflow";
  }

  len = snprintf(buf + pos, remaining, "argc=%d", argc);
  if (len < 0 || (size_t)len >= remaining) {
    return "error: buffer overflow";
  }
  pos += (size_t)len;

  /* Add each argument */
  for (i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      continue;
    }

    remaining = bufsize - pos;
    if (remaining <= 1) {
      break;
    }

    len = snprintf(buf + pos, remaining, " argv[%d]='%s'", i, argv[i]);
    if (len < 0 || (size_t)len >= remaining) {
      break;
    }
    pos += (size_t)len;
  }

  return buf;
}

int32_t rpc_register(const char *name, rpc_cb func) {
  /* For API compatibility, not used in this example */
  if ((name == NULL) || (func == NULL)) {
    return RPC_ERR_INVALID_PARAM;
  }
  return RPC_ERR_SUCCESS;
}

int32_t register_str_func(const char *name, rpc_string_cb func) {
  size_t name_len;

  if ((name == NULL) || (func == NULL)) {
    return RPC_ERR_INVALID_PARAM;
  }

  if (g_ctx.function_count >= MAX_FUNCTIONS) {
    return RPC_ERR_MAX_FUNCTIONS_REACHED;
  }

  name_len = strlen(name);
  if (name_len >= sizeof(g_ctx.functions[0].name)) {
    name_len = sizeof(g_ctx.functions[0].name) - 1;
  }

  memcpy(g_ctx.functions[g_ctx.function_count].name, name, name_len);
  g_ctx.functions[g_ctx.function_count].name[name_len] = '\0';
  g_ctx.functions[g_ctx.function_count].func = func;
  g_ctx.function_count++;

  return RPC_ERR_SUCCESS;
}

static const char *call_function(const char *name, int32_t argc, char **argv,
                                 char *buf, size_t bufsize) {
  if (name == NULL) {
    return "-1";
  }

  for (uint32_t i = 0; i < g_ctx.function_count; i++) {
    if (strcmp(name, g_ctx.functions[i].name) == 0) {
      if (g_ctx.functions[i].func != NULL) {
        return g_ctx.functions[i].func(argc, argv, buf, bufsize);
      }
    }
  }

  /* Call echo with the new set of arguments */
  return echo_func(argc, argv, buf, bufsize);
}

static void send_result(const char *result, const client_info_t *client) {
  char buffer[RPC_MAX_PACKET_SIZE];
  int32_t len;
  ssize_t sent_bytes;

  if (client == NULL) {
    return;
  }

  if (result == NULL) {
    result = "";
  }

  len = snprintf(buffer, sizeof(buffer), "%s", result);
  if ((len < 0) || ((size_t)len >= sizeof(buffer))) {
    return;
  }

  sent_bytes = sendto(g_ctx.sock_fd, buffer, (size_t)len, 0,
                      (struct sockaddr *)&client->addr, client->addr_len);
  if (sent_bytes < 0) {
    RPC_LOG("error send res error='%s'", strerror(errno));
  }
}

static int32_t parse_args(char *buffer, size_t bufsize, int32_t *argc_ptr,
                          char ***argv_ptr, size_t argv_size) {
  size_t arg_count = 0;
  char **argv;
  const char *p;
  const char *end;
  const char *check;

  if ((buffer == NULL) || (argc_ptr == NULL) || (argv_ptr == NULL)) {
    return EINVAL;
  }

  /* Check that buffer size is valid */
  if (bufsize == 0) {
    return EINVAL;
  }

  argv = *argv_ptr;
  p = buffer;
  end = buffer + bufsize;

  while ((p < end) && (arg_count < argv_size - 1)) {
    /* Skip consecutive zeros (find argument start) */
    while (p < end && *p == '\0') {
      p++;
    }
    if (p >= end) {
      break; /* Reached end of buffer */
    }

    /* Record argument start */
    argv[arg_count] = (char *)p;
    arg_count++;

    /* Skip to next null or end of buffer */
    while (p < end && *p != '\0') {
      p++;
    }

    /* Break if reached end of buffer */
    if (p >= end) {
      break;
    }
  }

  /* Check if there are unprocessed arguments in buffer */
  if (p < end) {
    /* Check if there are any non-null chars left */
    check = p;
    while (check < end && *check == '\0') {
      check++;
    }
    if (check < end) {
      return EOVERFLOW;
    }
  }

  /* Null-terminate the argv array */
  argv[arg_count] = NULL;

  /* Check for integer overflow in argc */
  if (arg_count > (size_t)INT_MAX) {
    return EINVAL;
  }

  *argc_ptr = (int32_t)arg_count;
  return 0;
}

static int32_t rpc_handle_request(char *buffer, ssize_t recv_size,
                                  client_info_t *client) {
  char *argv[MAX_ARGS];
  char **argv_ptr = argv;
  int32_t argc = 0;
  const char *result;
  int32_t parse_result;

  if (buffer == NULL || client == NULL) {
    return RPC_ERR_INVALID_PARAM;
  }

  RPC_LOG("recv_size=%zd", recv_size);

  /* Validate received size */
  if (recv_size < 0 || recv_size >= RPC_MAX_PACKET_SIZE) {
    return RPC_ERR_INVALID_PARAM;
  }

  /* Null-terminate the buffer */
  buffer[recv_size] = '\0';

  /* Parse arguments */
  parse_result =
      parse_args(buffer, (size_t)recv_size, &argc, &argv_ptr, MAX_ARGS);
  if (parse_result != 0) {
    RPC_LOG("error parsing arguments res=%d", parse_result);
    return RPC_ERR_PARSE_ERROR;
  }

  /* Call the function if we have at least one argument (function name) */
  if (argc > 0 && argv[0] != NULL) {
    char buf[4096];
    buf[0] = '\0';
    RPC_LOG("call func=%s argc=%d", argv[0], argc - 1);
    result = call_function(argv[0], argc - 1, &argv[1], buf, sizeof(buf));
    send_result(result, client);
    return RPC_ERR_SUCCESS;
  }

  return RPC_ERR_INVALID_PARAM;
}

static void *rpc_server_thread(void *arg) {
  fd_set read_fds;
  int32_t ready;
  char buffer[RPC_MAX_PACKET_SIZE];
  ssize_t recv_len;
  client_info_t client;
  struct timespec timeout;

  /* Avoid unused parameter warning */
  (void)arg;

  while (atomic_load(&g_ctx.keep_running)) {
    /* Initialize variables for each iteration */
    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;
    client.addr_len = sizeof(client.addr);

    /* Set up select */
    FD_ZERO(&read_fds);
    FD_SET(g_ctx.sock_fd, &read_fds);

    ready = pselect(g_ctx.sock_fd + 1, &read_fds, NULL, NULL, &timeout, NULL);

    /* Handle select result */
    if (ready < 0) {
      if (errno == EINTR) {
        continue; /* Interrupted by signal */
      }
      RPC_LOG("error in pselect error='%s'", strerror(errno));
      break;
    }

    if (ready > 0 && FD_ISSET(g_ctx.sock_fd, &read_fds)) {
      /* Clear buffer before receiving data */
      memset(buffer, 0, sizeof(buffer));

      /* Receive data */
      recv_len = recvfrom(g_ctx.sock_fd, buffer, sizeof(buffer) - 1, 0,
                          (struct sockaddr *)&client.addr, &client.addr_len);

      /* Process received data */
      if (recv_len <= 0) {
        RPC_LOG("error in recvfrom: %s", strerror(errno));
        continue;
      }

      /* Handle the request */
      RPC_LOG("buf=%zd '%s'", recv_len, buffer);
      rpc_handle_request(buffer, recv_len, &client);
    }
  }

  return NULL;
}

int rpc_get_default_path(const char *bin_name, char *path, size_t path_size)
{
    const char *base;
    char bin_copy[256];
    int ret;

    if (bin_name == NULL || path == NULL || path_size < RPC_SOCKET_PATH_MAX) {
        return -1;
    }

    strncpy(bin_copy, bin_name, sizeof(bin_copy) - 1);
    bin_copy[sizeof(bin_copy) - 1] = '\0';
    base = basename(bin_copy);

    ret = snprintf(path, path_size, "/var/run/%s.sock", base);
    if (ret < 0 || (size_t)ret >= path_size) {
        return -1;
    }

    return 0;
}

int32_t rpc_client_call(const char *socket_path, int32_t argc,
                        char **argv, char *response, size_t response_size) {
  int32_t client_sock;
  struct sockaddr_un server_addr;
  ssize_t bytes_sent, bytes_received;
  struct timeval tv;
  char request_buffer[RPC_MAX_PACKET_SIZE];
  int32_t i = 0;
  size_t pos = 0;
  int32_t len;
  size_t remaining;
  size_t path_len;

  /* Parameter validation */
  if (argc < 1 || argv == NULL || socket_path == NULL || response == NULL ||
      response_size == 0) {
    return RPC_ERR_INVALID_PARAM;
  }

  path_len = strlen(socket_path);
  if (path_len >= sizeof(server_addr.sun_path)) {
    return RPC_ERR_INVALID_PARAM;
  }

  /* Build request string with null-byte delimiters */
  for (i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      continue;
    }

    remaining = RPC_MAX_PACKET_SIZE - pos - 1;
    if (remaining <= 1) {
      break;
    }

    len = snprintf(request_buffer + pos, remaining, "%s%c", argv[i], '\0');
    if (len < 0 || (size_t)len >= remaining) {
      break;
    }
    pos += (size_t)len;
  }

  if (pos < RPC_MAX_PACKET_SIZE) {
    request_buffer[pos] = '\0';
  } else {
    request_buffer[RPC_MAX_PACKET_SIZE - 1] = '\0';
  }

  /* Create Unix domain socket */
  client_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (client_sock < 0) {
    RPC_LOG("error create socket error='%s'", strerror(errno));
    return RPC_ERR_SOCKET_ERROR;
  }

  /* Set socket timeout */
  tv.tv_sec = RPC_DEFAULT_TIMEOUT_SEC;
  tv.tv_usec = 0;
  if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    RPC_LOG("error set socket error='%s'", strerror(errno));
    close(client_sock);
    return RPC_ERR_SYSTEM;
  }

  /* Configure server address */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sun_family = AF_UNIX;
  strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);
  server_addr.sun_path[sizeof(server_addr.sun_path) - 1] = '\0';

  /* Connect to server */
  if (connect(client_sock, (struct sockaddr *)&server_addr,
              sizeof(server_addr)) < 0) {
    RPC_LOG("error connect error='%s'", strerror(errno));
    close(client_sock);
    return RPC_ERR_NETWORK;
  }

  /* Send request to server */
  bytes_sent = send(client_sock, request_buffer, pos, 0);
  if (bytes_sent < 0) {
    RPC_LOG("error send error='%s'", strerror(errno));
    close(client_sock);
    return RPC_ERR_NETWORK;
  }

  /* Receive response from server */
  bytes_received = recv(client_sock, response, response_size - 1, 0);

  close(client_sock);

  if (bytes_received < 0) {
    RPC_LOG("error recv res='%s'", strerror(errno));
    return RPC_ERR_NETWORK;
  }

  /* Null-terminate the response */
  response[bytes_received] = '\0';
  return RPC_ERR_SUCCESS;
}

rpc_context_t *rpc_init(const char *socket_path) {
  struct sockaddr_un server_addr;
  size_t path_len;
  const char *default_path = NULL;

  /* Initialize the keep_running flag */
  atomic_store(&g_ctx.keep_running, true);

  if (socket_path == NULL) {
    if (rpc_get_default_path("kmodlike", g_socket_path,
                sizeof(g_socket_path)) != 0) {
      RPC_LOG("error getting default socket path");
      return NULL;
    }
    default_path = g_socket_path;
    socket_path = default_path;
  } else {
    size_t len = strlen(socket_path);
    if (len >= sizeof(g_socket_path)) {
        RPC_LOG("socket path too long");
        return NULL;
    }
    strncpy(g_socket_path, socket_path, sizeof(g_socket_path) - 1);
    g_socket_path[sizeof(g_socket_path) - 1] = '\0';
  }

  path_len = strlen(socket_path);
  if (path_len >= sizeof(server_addr.sun_path)) {
    RPC_LOG("socket path too long");
    return NULL;
  }

  path_len = strlen(socket_path);
  if (path_len >= sizeof(server_addr.sun_path)) {
    RPC_LOG("socket path too long");
    return NULL;
  }

  /* Remove existing socket file if it exists */
  unlink(socket_path);

  /* Create Unix domain socket */
  g_ctx.sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (g_ctx.sock_fd < 0) {
    RPC_LOG("error create socket: %s", strerror(errno));
    return NULL;
  }

  /* Configure server address */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sun_family = AF_UNIX;
  memcpy(server_addr.sun_path, socket_path, path_len + 1);

  /* Bind socket to address */
  if (bind(g_ctx.sock_fd, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    /* If bind to /var/run failed and using default path, try /tmp */
    if (default_path != NULL && errno == EACCES) {
      RPC_LOG("cannot bind to %s, trying /tmp fallback", socket_path);
      close(g_ctx.sock_fd);

      {
        char bin_name[256];
        char name_no_ext[256];
        const char *base;
        size_t base_len;
        strncpy(bin_name, default_path, sizeof(bin_name) - 1);
        bin_name[sizeof(bin_name) - 1] = '\0';
        base = basename(bin_name);
        base_len = strlen(base);
        if (base_len >= 5 && strcmp(base + base_len - 5, ".sock") == 0) {
          strncpy(name_no_ext, base, base_len - 5);
          name_no_ext[base_len - 5] = '\0';
          snprintf(g_socket_path, sizeof(g_socket_path), "/tmp/%s.sock", name_no_ext);
        } else {
          snprintf(g_socket_path, sizeof(g_socket_path), "/tmp/%s.sock", base);
        }
      }
      socket_path = g_socket_path;
      path_len = strlen(socket_path);
      unlink(socket_path);

      g_ctx.sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
      if (g_ctx.sock_fd < 0) {
        RPC_LOG("error create socket: %s", strerror(errno));
        return NULL;
      }

      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sun_family = AF_UNIX;
      memcpy(server_addr.sun_path, socket_path, path_len + 1);

      if (bind(g_ctx.sock_fd, (struct sockaddr *)&server_addr,
               sizeof(server_addr)) < 0) {
        RPC_LOG("bind socket error=%s", strerror(errno));
        close(g_ctx.sock_fd);
        g_ctx.sock_fd = -1;
        return NULL;
      }
    } else {
      RPC_LOG("bind socket error=%s", strerror(errno));
      close(g_ctx.sock_fd);
      g_ctx.sock_fd = -1;
      return NULL;
    }
  }

  /* Start server thread */
  if (pthread_create(&g_ctx.server_thread, NULL, rpc_server_thread, NULL) !=
      0) {
    RPC_LOG("create server thread error=%s", strerror(errno));
    close(g_ctx.sock_fd);
    unlink(socket_path);
    g_ctx.sock_fd = -1;
    return NULL;
  }

  return &g_ctx;
}

int rpc_get_socket_path(char *path, size_t path_size)
{
    if (path == NULL || path_size < RPC_SOCKET_PATH_MAX) {
        return -1;
    }

    if (g_socket_path[0] == '\0') {
        return -1;
    }

    strncpy(path, g_socket_path, path_size - 1);
    path[path_size - 1] = '\0';
    return 0;
}

int rpc_deinit() {
  /* check if rpc server is running */
  if (!atomic_load(&g_ctx.keep_running)) {
    return EINVAL;
  }
  /* Signal the server thread to exit */
  atomic_store(&g_ctx.keep_running, false);

  /* Join the server thread */
  if (pthread_join(g_ctx.server_thread, NULL) != 0) {
    RPC_LOG("failed to join server thread error=%s", strerror(errno));
  }

  /* Close the socket */
  if (g_ctx.sock_fd >= 0) {
    close(g_ctx.sock_fd);
    g_ctx.sock_fd = -1;
  }

  /* Remove socket file */
  if (g_socket_path[0] != '\0') {
    unlink(g_socket_path);
    g_socket_path[0] = '\0';
  }

  return 0;
}
