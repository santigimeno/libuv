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


#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>

#include "uv.h"
#include "task.h"

int checks = 0;
int loop = 1;
int running[2] = {0};
int fd[3] = {0};
pthread_t pth[2];

static void server_poll_cb(uv_poll_t* handle, int status, int events) {
	int c, n;

	if(events & UV_PRIORITIZED) {
		n = recv(fd[1], &c, 1, MSG_OOB);
		ASSERT(c == 2);
		ASSERT(n == 1);
		__sync_add_and_fetch(&checks, 1);
		uv_poll_stop(handle);
	}
}

static void client_poll_cb(uv_poll_t* handle, int status, int events) {
	int n, c;
	c = 2;

	if(events & UV_WRITABLE) {
		n = send(fd[0], &c, 1, MSG_OOB);
		ASSERT(n >= 0);
		__sync_add_and_fetch(&checks, 1);
		uv_poll_stop(handle);
	}
}

static void *thread_srv(void *a) {
	uv_poll_t poll_req;
	struct sockaddr_in servaddr, cliaddr;
	unsigned int clilen;
	int r;

	fd[2] = socket(PF_INET, SOCK_STREAM, 0);
	ASSERT(fd[2] >= 0);

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(TEST_PORT);

#ifndef _WIN32
  {
    int yes = 1;
    r = setsockopt(fd[2], SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    ASSERT(r == 0);
  }
#endif	
	
	r = bind(fd[2], (struct sockaddr *) &servaddr, sizeof(servaddr));
	ASSERT(r >= 0);

	listen(fd[2], 1);
	clilen = sizeof(cliaddr);

	fd[1] = accept(fd[2], (struct sockaddr *)&cliaddr, &clilen);
	ASSERT(fd[1] >= 0);

	uv_poll_init(uv_default_loop(), &poll_req, fd[1]);
  r = uv_poll_start(&poll_req, UV_PRIORITIZED, server_poll_cb);
  ASSERT(r == 0);

	while(loop) {
		usleep(10);
	}
	running[0] = 0;

	return 0;
}

static void *thread_cli(void *a) {
	uv_poll_t poll_req;
	int r;
	struct sockaddr_in serv_addr;
	struct hostent *server;

	fd[0] = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT(fd[0] >= 0);

	server = gethostbyname("127.0.0.1");
  ASSERT(server != NULL); 

	serv_addr.sin_family = AF_INET;
	memcpy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(TEST_PORT);

	r = connect(fd[0], (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	ASSERT(r >= 0);

	uv_poll_init(uv_default_loop(), &poll_req, fd[0]);
  r = uv_poll_start(&poll_req, UV_WRITABLE, client_poll_cb);
  ASSERT(r == 0);	
	while(loop) {
		usleep(10);
	}
	running[1] = 0;
  return 0;
}

TEST_IMPL(poll_oob) {
	pthread_create(&pth[0], NULL, thread_srv, NULL);
	sleep(1);
	pthread_create(&pth[1], NULL, thread_cli, NULL);
	sleep(1);
	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	loop = 0;
	while(running[0] == 1 && running[1] == 0) {
		usleep(10);
	}
	pthread_join(pth[0], NULL);
	pthread_join(pth[1], NULL);

	ASSERT(checks == 2);
	return 0;
}
