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

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_HANDLE(handle) \
  ASSERT((uv_udp_t*)(handle) == &server || (uv_udp_t*)(handle) == &client)

static uv_udp_t server;
static uv_udp_t client;

static int cl_recv_cb_called;

static int sv_send_cb_called;

static int close_cb_called;

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  CHECK_HANDLE(handle);
  ASSERT(suggested_size <= sizeof(slab));
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  close_cb_called++;
}


static void sv_send_cb(uv_udp_send_t* req, int status) {
  ASSERT(req != NULL);
  ASSERT(status == 0);
  CHECK_HANDLE(req->handle);

  sv_send_cb_called++;

  uv_close((uv_handle_t*) req->handle, close_cb);
}


static void cl_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf,
                       const struct sockaddr* addr,
                       unsigned flags) {
  CHECK_HANDLE(handle);
  ASSERT(flags == 0);

  cl_recv_cb_called++;

  if (nread < 0) {
    ASSERT(0 && "unexpected error");
  }

  if (nread == 0) {
    /* Returning unused buffer. Don't count towards cl_recv_cb_called */
    ASSERT(addr == NULL);
    return;
  }

  ASSERT(addr != NULL);
  ASSERT(nread == 4);
  ASSERT(!memcmp("PING", buf->base, nread));

  /* we are done with the client handle, we can close it */
  uv_close((uv_handle_t*) &client, close_cb);
}


TEST_IMPL(udp_ss_multicast_join6) {
  int i, r;
  uv_udp_send_t req;
  uv_buf_t buf;
  struct sockaddr_in6 addr;
  uv_interface_address_t* iface_addresses;
  uv_interface_address_t iface_addr;
  int count;
  char buffer[64];

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT(0 == uv_ip6_addr("::", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT(r == 0);

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT(r == 0);

  /* bind to the desired port */
  r = uv_udp_bind(&client, (const struct sockaddr*) &addr, 0);
  ASSERT(r == 0);

  r = uv_interface_addresses(&iface_addresses, &count);
  ASSERT(r == 0);

  for (i = 0; i < count; i += 1) {
    iface_addr = iface_addresses[i];
    if (iface_addr.address.address6.sin6_family == AF_INET6&&
        strncmp(iface_addr.name, "lo", 2) != 0) {
      uv_ip6_name(&iface_addr.address.address6, buffer, sizeof(buffer));
      /* join the multicast channel */
      fprintf(stderr, "buffer: %s", buffer);
      r = uv_udp_set_source_membership(&client, "ff02::1", NULL, buffer, UV_JOIN_GROUP);
      if (r == UV_ENODEV)
        RETURN_SKIP("No multicast support.");
      ASSERT(r == 0);
    }
  }

  uv_free_interface_addresses(iface_addresses, count);

  r = uv_udp_recv_start(&client, alloc_cb, cl_recv_cb);
  ASSERT(r == 0);

  buf = uv_buf_init("PING", 4);

  ASSERT(0 == uv_ip6_addr("ff02::1", TEST_PORT, &addr));

  /* server sends "PING" */
  r = uv_udp_send(&req,
                  &server,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  sv_send_cb);
  ASSERT(r == 0);

  ASSERT(close_cb_called == 0);
  ASSERT(cl_recv_cb_called == 0);
  ASSERT(sv_send_cb_called == 0);

  /* run the loop till all events are processed */
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(cl_recv_cb_called == 1);
  ASSERT(sv_send_cb_called == 1);
  ASSERT(close_cb_called == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
