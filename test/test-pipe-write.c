#include "uv.h"
#include "task.h"

static uv_pipe_t client_handle;
static uv_pipe_t peer_handle;
static uv_pipe_t server_handle;
static uv_write_t write_req;


static void write_cb(uv_write_t* req, int status) {
  ASSERT_EQ(0, status);
}


static void do_write(uv_pipe_t* handle) {
  uv_buf_t buf;

  buf = uv_buf_init("hello, world", sizeof("hello, world") - 1);
  ASSERT_EQ(0, uv_write(&write_req, (uv_stream_t*) handle, &buf, 1, write_cb));
}


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char base[256];

  buf->base = base;
  buf->len = sizeof(base);
}


static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_UINT64_GT(nread, 0);
  if (handle == (uv_stream_t*) &client_handle) {
    do_write(&client_handle);
  } else {
    uv_close((uv_handle_t*) &peer_handle, NULL);
    uv_close((uv_handle_t*) &client_handle, NULL);
    uv_close((uv_handle_t*) &server_handle, NULL);
  }
}


static void connection_cb(uv_stream_t* handle, int status) {
  ASSERT_EQ(0, status);
  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &peer_handle, 0));
  ASSERT_EQ(0, uv_accept((uv_stream_t*) &server_handle,
                         (uv_stream_t*) &peer_handle));
  ASSERT_EQ(0, uv_read_start((uv_stream_t*) &peer_handle, alloc_cb, read_cb));
  do_write(&peer_handle);
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_EQ(0, status);
  ASSERT_EQ(0, uv_read_start((uv_stream_t*) &client_handle, alloc_cb, read_cb));
}


TEST_IMPL(pipe_write) {
  uv_connect_t connect_req;

  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &client_handle, 0));
  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &server_handle, 0));
  ASSERT_EQ(0, uv_pipe_bind(&server_handle, TEST_PIPENAME));
  ASSERT_EQ(0, uv_listen((uv_stream_t*) &server_handle, 1, connection_cb));
  uv_pipe_connect(&connect_req, &client_handle, TEST_PIPENAME, connect_cb);
  ASSERT_EQ(0, uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY();
  return 0;
}
