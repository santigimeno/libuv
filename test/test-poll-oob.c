/* Copyright Fedor Indutny & CurlyMo All rights reserved.
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

#if !defined(_WIN32)

#include "uv.h"
#include "task.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

static uv_tcp_t server_handle;
static uv_tcp_t client_handle;
static uv_tcp_t peer_handle;
static uv_poll_t poll_req;
static uv_idle_t idle;
static uv_os_fd_t client_fd;
static uv_connect_t connect_req;
static int ticks;
static const int kMaxTicks = 10;
static int check = 0;

static void idle_cb(uv_idle_t* idle) {
	usleep(100);
  if (++ticks < kMaxTicks)
    return;

	
	uv_poll_stop(&poll_req);
  uv_close((uv_handle_t*) &server_handle, NULL);
  uv_close((uv_handle_t*) &client_handle, NULL);
  uv_close((uv_handle_t*) &peer_handle, NULL);
  uv_close((uv_handle_t*) idle, NULL);
}

static void poll_cb(uv_poll_t* handle, int status, int events) {
	char buffer[5];

	if(events & UV_PRIORITIZED) {
		int n = recv(client_fd, &buffer, 5, MSG_OOB);
		if(errno == EINVAL) {
			return;
		}
		check = 1;
		ASSERT(n > 0);
	}
}

static void connect_cb(uv_connect_t* req, int status) {
  ASSERT(req->handle == (uv_stream_t*) &client_handle);
  ASSERT(0 == status);
}

static int non_blocking(int fd, int set) {
	int r;

	do
    r = ioctl(client_fd, FIONBIO, &set);
  while (r == -1 && errno == EINTR);

  if(r)
    return -errno;

  return 0;
}

static void connection_cb(uv_stream_t* handle, int status) {
  uv_os_fd_t server_fd;
  int r;

  ASSERT(0 == status);
  ASSERT(0 == uv_accept(handle, (uv_stream_t*) &peer_handle));

  /* Send some OOB data */
  ASSERT(0 == uv_fileno((uv_handle_t*) &peer_handle, &server_fd));
  ASSERT(0 == non_blocking(client_fd, 1));

	uv_poll_init(uv_default_loop(), &poll_req, client_fd);
	ASSERT(0 == uv_poll_start(&poll_req, UV_PRIORITIZED, poll_cb));

  /* The problem triggers only on a second message, it seem that xnu is not
   * triggering `kevent()` for the first one
   */
  do {
    r = send(server_fd, "hello", 5, MSG_OOB);
  } while (r < 0 && errno == EINTR);
  ASSERT(5 == r);

  do {
    r = send(server_fd, "hello", 5, MSG_OOB);
  } while (r < 0 && errno == EINTR);
  ASSERT(5 == r);

  ASSERT(0 == non_blocking(client_fd, 0));

  ASSERT(0 == uv_idle_start(&idle, idle_cb));
}


TEST_IMPL(poll_oob) {
  struct sockaddr_in addr;
	int addrlen = sizeof(addr), r = 0;
  uv_loop_t* loop;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  loop = uv_default_loop();

  ASSERT(0 == uv_tcp_init(loop, &server_handle));
  ASSERT(0 == uv_tcp_init(loop, &client_handle));
  ASSERT(0 == uv_tcp_init(loop, &peer_handle));
  ASSERT(0 == uv_idle_init(loop, &idle));
  ASSERT(0 == uv_tcp_bind(&server_handle, (const struct sockaddr*) &addr, 0));
  ASSERT(0 == uv_listen((uv_stream_t*) &server_handle, 1, connection_cb));

  /* Ensure two separate packets */
  ASSERT(0 == uv_tcp_nodelay(&client_handle, 1));

	client_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT(client_fd >= 0);
	do {
    errno = 0;
    r = connect(client_fd, (const struct sockaddr*)&addr, addrlen);
  } while (r == -1 && errno == EINTR);
	ASSERT(r == 0);

  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));

  ASSERT(ticks == kMaxTicks);
	ASSERT(check == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
#endif
