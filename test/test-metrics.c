#include "uv.h"
#include "task.h"
#include <string.h> /* memset */


typedef struct {
  int metrics_run_count;
  int timer_run_count;
} mtc_t;

typedef struct {
  uv_async_t async;
  uv_loop_t mloop;
  uv_timer_t timer;
  uv_thread_t thread;
  uv_timer_t ttimer;
  int thread_timer_count;
} mthread_t;


static void timer_noop_cb(uv_timer_t* handle) {
}

TEST_IMPL(metrics_idle_time) {
  uv_timer_t timer;
  int r;
  uint64_t pt;

  uv_loop_configure(uv_default_loop(), UV_LOOP_IDLE_TIME);
  uv_timer_init(uv_default_loop(), &timer);
  uv_timer_start(&timer, timer_noop_cb, 100, 0);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(r == 0);

  pt = uv_metrics_idle_time(uv_default_loop());

  /* Technically should be able to check if >= 100 ms, but the timer might run
   * slightly early. So check if >= expected time - 1 ms.
   */
  ASSERT(pt >= 99000000);

  return 0;
}


static void metrics_thread_timer_cb(uv_timer_t* handle) {
  mthread_t* mthread = (mthread_t*) handle->data;
  uint64_t pt = uv_metrics_idle_time(&mthread->mloop);
  uint64_t ptt = uv_metrics_idle_time(handle->loop);
  mthread->thread_timer_count++;
  /* Technically should be able to check if >= 100 ms, but the timer might run
   * slightly early. So check if >= expected time - 1 ms.
   */
  ASSERT(pt >= (100000000 * mthread->thread_timer_count) - 1000000);
  ASSERT(ptt >= (100000000 * mthread->thread_timer_count) - 1000000);
  if (mthread->thread_timer_count > 2) {
    uv_timer_stop(&mthread->ttimer);
    uv_async_send(&mthread->async);
  }
}

static void metrics_thread_routine_cb(void* arg) {
  mthread_t* mthread = (mthread_t*) arg;
  uv_loop_t loop;
  int r;

  uv_loop_init(&loop);
  uv_loop_configure(&loop, UV_LOOP_IDLE_TIME);
  uv_timer_init(&loop, &mthread->ttimer);
  uv_timer_start(&mthread->ttimer, metrics_thread_timer_cb, 100, 100);

  mthread->ttimer.data = mthread;

  r = uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT(r == 0);

  uv_loop_close(&loop);
}

static void metrics_thread_async_cb(uv_async_t* handle) {
  mthread_t* mthread = (mthread_t*) handle->data;
  uv_timer_stop(&mthread->timer);
  uv_close((uv_handle_t*) &mthread->async, NULL);
}

TEST_IMPL(metrics_idle_time_thread) {
  mthread_t mthread;
  int r;

  memset(&mthread, 0, sizeof(mthread));
  mthread.thread_timer_count = 0;

  uv_loop_init(&mthread.mloop);
  uv_loop_configure(&mthread.mloop, UV_LOOP_IDLE_TIME);
  uv_async_init(&mthread.mloop, &mthread.async, metrics_thread_async_cb);
  uv_timer_init(&mthread.mloop, &mthread.timer);
  uv_timer_start(&mthread.timer, timer_noop_cb, 100, 100);
  uv_thread_create(&mthread.thread, metrics_thread_routine_cb, &mthread);

  mthread.async.data = &mthread;

  r = uv_run(&mthread.mloop, UV_RUN_DEFAULT);
  ASSERT(r == 0);

  uv_thread_join(&mthread.thread);
  uv_loop_close(&mthread.mloop);

  ASSERT(mthread.thread_timer_count == 3);

  return 0;
}
