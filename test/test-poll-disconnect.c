/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>

#ifndef _WIN32
# include <sys/socket.h>
# include <unistd.h>
#endif

#include "uv.h"
#include "task.h"


typedef struct connection_context_s {
  uv_poll_t poll_handle;
  uv_os_sock_t sock;
  int is_server_connection;
  unsigned int events;
  int got_fin, sent_fin;
} connection_context_t;

typedef struct server_context_s {
  uv_poll_t poll_handle;
  uv_os_sock_t sock;
  int connections;
} server_context_t;


static int got_eagain(void) {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN
      || errno == EINPROGRESS
#ifdef EWOULDBLOCK
      || errno == EWOULDBLOCK;
#endif
      ;
#endif
}


static uv_os_sock_t create_bound_socket (struct sockaddr_in bind_addr) {
  uv_os_sock_t sock;
  int r;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
#ifdef _WIN32
  ASSERT(sock != INVALID_SOCKET);
#else
  ASSERT(sock >= 0);
#endif

#ifndef _WIN32
  {
    /* Allow reuse of the port. */
    int yes = 1;
    r = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    ASSERT(r == 0);
  }
#endif

  r = bind(sock, (const struct sockaddr*) &bind_addr, sizeof bind_addr);
  ASSERT(r == 0);

  return sock;
}


static void close_socket(uv_os_sock_t sock) {
  int r;
#ifdef _WIN32
  r = closesocket(sock);
#else
  r = close(sock);
#endif
  ASSERT(r == 0);
}


static connection_context_t* create_connection_context(
    uv_os_sock_t sock, int is_server_connection) {
  int r;
  connection_context_t* context;

  context = (connection_context_t*) malloc(sizeof *context);
  ASSERT(context != NULL);

  context->sock = sock;
  context->is_server_connection = is_server_connection;
  context->events = 0;
  context->got_fin = 0;
  context->sent_fin = 0;

  r = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, sock);
  context->poll_handle.data = context;
  ASSERT(r == 0);

  return context;
}


static void connection_close_cb(uv_handle_t* handle) {
  connection_context_t* context = (connection_context_t*) handle->data;
  free(context);
}


static void destroy_connection_context(connection_context_t* context) {
  uv_close((uv_handle_t*) &context->poll_handle, connection_close_cb);
}

static void client_connection_poll_cb(uv_poll_t* handle, int status, int events) {
  connection_context_t* context = (connection_context_t*) handle->data;
  unsigned int new_events;
  int r;
  char buffer[4];

  ASSERT(status == 0);
  ASSERT(events & context->events);
  ASSERT(!(events & ~context->events));

  new_events = context->events;

  if (events & UV_DISCONNECT) {
    fprintf(stderr, "CLIENT DISCONNECT\n");
  }

  if (events & UV_READABLE) {
    fprintf(stderr, "CLIENT READABLE\n");
    r = recv(context->sock, buffer, sizeof buffer, 0);
    ASSERT(r == 0);

    /* Sent and received FIN. Close and destroy context. */
    context->events = 0;
  }

  if (events & UV_WRITABLE)  {
    fprintf(stderr, "CLIENT WRITABLE\n");
    r = send(context->sock, "ping", 4, 0);
    ASSERT(r == 4);
    new_events &= ~UV_WRITABLE;
  }

  if (context->events == 0) {
    close_socket(context->sock);
    destroy_connection_context(context);
  } else if (new_events != context->events) {
    /* Poll mask changed. Call uv_poll_start again. */
    context->events = new_events;
    uv_poll_start(handle, new_events, client_connection_poll_cb);
  }

  /* Assert that uv_is_active works correctly for poll handles. */
  if (context->events != 0) {
    ASSERT(1 == uv_is_active((uv_handle_t*) handle));
  } else {
    ASSERT(0 == uv_is_active((uv_handle_t*) handle));
  }
}

static void server_connection_poll_cb(uv_poll_t* handle, int status, int events) {
  connection_context_t* context = (connection_context_t*) handle->data;
  unsigned int new_events;
  int r;
  char buffer[4];

  ASSERT(status == 0);
  ASSERT(events & context->events);
  ASSERT(!(events & ~context->events));

  new_events = context->events;

  if (events & UV_READABLE) {
    fprintf(stderr, "SERVER UV_READABLE\n");
    r = recv(context->sock, buffer, sizeof buffer, 0);
    if (context->sent_fin == 0) {
      ASSERT(r == 4);
      /* Send FIN. */
      int r;
  #ifdef _WIN32
        r = shutdown(context->sock, SD_SEND);
  #else
        r = shutdown(context->sock, SHUT_WR);
  #endif
      ASSERT(r == 0);
      context->sent_fin = 1;
    } else {
      ASSERT(r == 0);
      close_socket(context->sock);
      destroy_connection_context(context);
      context->events = 0;
    }
  }

  /* Assert that uv_is_active works correctly for poll handles. */
  if (context->events != 0) {
    ASSERT(1 == uv_is_active((uv_handle_t*) handle));
  } else {
    ASSERT(0 == uv_is_active((uv_handle_t*) handle));
  }
}

static server_context_t* create_server_context(uv_os_sock_t sock) {
  int r;
  server_context_t* context;

  context = (server_context_t*) malloc(sizeof *context);
  ASSERT(context != NULL);

  context->sock = sock;
  context->connections = 0;

  r = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, sock);
  context->poll_handle.data = context;
  ASSERT(r == 0);

  return context;
}


static void server_close_cb(uv_handle_t* handle) {
  server_context_t* context = (server_context_t*) handle->data;
  free(context);
}


static void destroy_server_context(server_context_t* context) {
  uv_close((uv_handle_t*) &context->poll_handle, server_close_cb);
}


static void server_poll_cb(uv_poll_t* handle, int status, int events) {
  server_context_t* server_context = (server_context_t*)handle->data;
  connection_context_t* connection_context;
  struct sockaddr_in addr;
  socklen_t addr_len;
  uv_os_sock_t sock;
  int r;

  addr_len = sizeof addr;
  sock = accept(server_context->sock, (struct sockaddr*) &addr, &addr_len);
#ifdef _WIN32
  ASSERT(sock != INVALID_SOCKET);
#else
  ASSERT(sock >= 0);
#endif

  connection_context = create_connection_context(sock, 1);
  connection_context->events = UV_READABLE;
  r = uv_poll_start(&connection_context->poll_handle,
                    UV_READABLE,
                    server_connection_poll_cb);
  ASSERT(r == 0);

  close_socket(server_context->sock);
  destroy_server_context(server_context);
}


static void start_server(void) {
  server_context_t* context;
  struct sockaddr_in addr;
  uv_os_sock_t sock;
  int r;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  sock = create_bound_socket(addr);
  context = create_server_context(sock);

  r = listen(sock, 1);
  ASSERT(r == 0);

  r = uv_poll_start(&context->poll_handle, UV_READABLE, server_poll_cb);
  ASSERT(r == 0);
}


static void start_client(void) {
  uv_os_sock_t sock;
  connection_context_t* context;
  struct sockaddr_in server_addr;
  struct sockaddr_in addr;
  int r;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  ASSERT(0 == uv_ip4_addr("0.0.0.0", 0, &addr));

  sock = create_bound_socket(addr);
  context = create_connection_context(sock, 0);

  context->events = UV_READABLE | UV_WRITABLE | UV_DISCONNECT;
  r = uv_poll_start(&context->poll_handle,
                    UV_READABLE | UV_WRITABLE | UV_DISCONNECT,
                    client_connection_poll_cb);
  ASSERT(r == 0);

  r = connect(sock, (struct sockaddr*) &server_addr, sizeof server_addr);
  ASSERT(r == 0 || got_eagain());
}


static void start_poll_test(void) {
  int r;

#ifdef _WIN32
  {
    struct WSAData wsa_data;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT(r == 0);
  }
#endif

  start_server();

  start_client();

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(r == 0);

  MAKE_VALGRIND_HAPPY();
}

TEST_IMPL(poll_disconnect) {
  start_poll_test();
  return 0;
}
