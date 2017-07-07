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
#include <string.h>

static uv_process_t process;
static uv_process_options_t options;
#define OUTPUT_SIZE 4096
static char output[OUTPUT_SIZE];
static int output_used;


static void close_cb(uv_handle_t* handle) {}
static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  uv_close((uv_handle_t*)process, close_cb);
}

static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = output + output_used;
  buf->len = OUTPUT_SIZE - output_used;
}


static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    output_used += nread;
  } else if (nread < 0) {
    ASSERT(nread == UV_EOF);
    uv_close((uv_handle_t*)tcp, close_cb);
  }
}


TEST_IMPL(platform_output) {
  char buffer[512];
  size_t rss;
  size_t size;
  double uptime;
  uv_pipe_t out;
  uv_stdio_container_t stdio[2];
  uv_rusage_t rusage;
  uv_cpu_info_t* cpus;
  uv_interface_address_t* interfaces;
  uv_passwd_t pwd;
  int count;
  int i;
  int r;
  int err;
  char* args[3];

  err = uv_get_process_title(buffer, sizeof(buffer));
  ASSERT(err == 0);
  printf("uv_get_process_title: %s\n", buffer);

  size = sizeof(buffer);
  err = uv_cwd(buffer, &size);
  ASSERT(err == 0);
  printf("uv_cwd: %s\n", buffer);

  err = uv_resident_set_memory(&rss);
#if defined(__CYGWIN__) || defined(__MSYS__)
  ASSERT(err == UV_ENOSYS);
#else
  ASSERT(err == 0);
  printf("uv_resident_set_memory: %llu\n", (unsigned long long) rss);
#endif

  err = uv_uptime(&uptime);
  ASSERT(err == 0);
  ASSERT(uptime > 0);
  printf("uv_uptime: %f\n", uptime);

  err = uv_getrusage(&rusage);
  ASSERT(err == 0);
  ASSERT(rusage.ru_utime.tv_sec >= 0);
  ASSERT(rusage.ru_utime.tv_usec >= 0);
  ASSERT(rusage.ru_stime.tv_sec >= 0);
  ASSERT(rusage.ru_stime.tv_usec >= 0);
  printf("uv_getrusage:\n");
  printf("  user: %llu sec %llu microsec\n",
         (unsigned long long) rusage.ru_utime.tv_sec,
         (unsigned long long) rusage.ru_utime.tv_usec);
  printf("  system: %llu sec %llu microsec\n",
         (unsigned long long) rusage.ru_stime.tv_sec,
         (unsigned long long) rusage.ru_stime.tv_usec);
  printf("  page faults: %llu\n", (unsigned long long) rusage.ru_majflt);
  printf("  maximum resident set size: %llu\n",
         (unsigned long long) rusage.ru_maxrss);

  err = uv_cpu_info(&cpus, &count);
#if defined(__CYGWIN__) || defined(__MSYS__)
  ASSERT(err == UV_ENOSYS);
#else
  ASSERT(err == 0);

  printf("uv_cpu_info:\n");
  for (i = 0; i < count; i++) {
    printf("  model: %s\n", cpus[i].model);
    printf("  speed: %d\n", cpus[i].speed);
    printf("  times.sys: %llu\n", (unsigned long long) cpus[i].cpu_times.sys);
    printf("  times.user: %llu\n",
           (unsigned long long) cpus[i].cpu_times.user);
    printf("  times.idle: %llu\n",
           (unsigned long long) cpus[i].cpu_times.idle);
    printf("  times.irq: %llu\n",  (unsigned long long) cpus[i].cpu_times.irq);
    printf("  times.nice: %llu\n",
           (unsigned long long) cpus[i].cpu_times.nice);
  }
#endif
  uv_free_cpu_info(cpus, count);

  err = uv_interface_addresses(&interfaces, &count);
  ASSERT(err == 0);

  uv_pipe_init(uv_default_loop(), &out, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*)&out;
  options.stdio_count = 2;
#ifdef _WIN32
  options.file = "ipconfig";
#else
  options.file = "/usr/sbin/ifconfig";
#endif
  args[0] = "/usr/sbin/ifconfig";
  args[1] = "-a";
  args[2] = NULL;
  options.args = args;
  options.flags = 0;
  options.exit_cb = exit_cb;
  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT(r == 0);
  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT(r == 0);
  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(r == 0);

  printf("uv_interface_addresses:\n");
  for (i = 0; i < count; i++) {
    char ether[18];
    printf("  name: %s\n", interfaces[i].name);
    printf("  internal: %d\n", interfaces[i].is_internal);
    printf("  physical address: ");
    sprintf(ether, "%02x:%02x:%02x:%02x:%02x:%02x",
           (unsigned char)interfaces[i].phys_addr[0],
           (unsigned char)interfaces[i].phys_addr[1],
           (unsigned char)interfaces[i].phys_addr[2],
           (unsigned char)interfaces[i].phys_addr[3],
           (unsigned char)interfaces[i].phys_addr[4],
           (unsigned char)interfaces[i].phys_addr[5]);
    printf("%s\n", ether);

    if (strcmp(ether, "00:00:00:00:00:00") != 0)
      ASSERT(strstr(output, ether) != 0);

    if (interfaces[i].address.address4.sin_family == AF_INET) {
      uv_ip4_name(&interfaces[i].address.address4, buffer, sizeof(buffer));
    } else if (interfaces[i].address.address4.sin_family == AF_INET6) {
      uv_ip6_name(&interfaces[i].address.address6, buffer, sizeof(buffer));
    }

    printf("  address: %s\n", buffer);

    if (interfaces[i].netmask.netmask4.sin_family == AF_INET) {
      uv_ip4_name(&interfaces[i].netmask.netmask4, buffer, sizeof(buffer));
      printf("  netmask: %s\n", buffer);
    } else if (interfaces[i].netmask.netmask4.sin_family == AF_INET6) {
      uv_ip6_name(&interfaces[i].netmask.netmask6, buffer, sizeof(buffer));
      printf("  netmask: %s\n", buffer);
    } else {
      printf("  netmask: none\n");
    }
  }
  uv_free_interface_addresses(interfaces, count);

  err = uv_os_get_passwd(&pwd);
  ASSERT(err == 0);

  printf("uv_os_get_passwd:\n");
  printf("  euid: %ld\n", pwd.uid);
  printf("  gid: %ld\n", pwd.gid);
  printf("  username: %s\n", pwd.username);
  printf("  shell: %s\n", pwd.shell);
  printf("  home directory: %s\n", pwd.homedir);

  return 0;
}

