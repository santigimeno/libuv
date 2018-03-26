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

uv_loop_t loop;
uv_tcp_t tcp_client;
uv_connect_t connection_request;


static void connect_cb(uv_connect_t *req, int status) {
  ASSERT(status == UV_ECANCELED);
}


TEST_IMPL(tcp_cancel_connect) {
  struct sockaddr_in sa;
  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &sa));
  ASSERT(0 == uv_loop_init(&loop));
  ASSERT(0 == uv_tcp_init(&loop, &tcp_client));

  ASSERT(0 == uv_tcp_connect(&connection_request,
                             &tcp_client,
                             (const struct sockaddr *)
                             &sa,
                             connect_cb));

  uv_close((uv_handle_t*) &tcp_client, NULL);


  uv_run(&loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

