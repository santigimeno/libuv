/* Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef UV_ATOMIC_OPS_H_
#define UV_ATOMIC_OPS_H_

#include "internal.h"  /* UV_UNUSED */

#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#include <atomic.h>
#endif

/* TODO(trevnorris): These are a GCC extension. Make them platform agnostic. */
#define UV_ATOMIC_LOAD(ptr, ret)                                              \
  do {                                                                        \
    __atomic_load(ptr, ret, __ATOMIC_CONSUME);                                \
  }                                                                           \
  while(0)

#define UV_ATOMIC_STORE(ptr, val)                                             \
  do {                                                                        \
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);                             \
  }                                                                           \
  while(0)

#define UV_ATOMIC_EXCHANGE(ptr, ret, val)                                     \
  do {                                                                        \
    *ret = __atomic_exchange_n(ptr, val, __ATOMIC_ACQ_REL);                   \
  }                                                                           \
  while (0)

#define UV_ATOMIC_ADD_FETCH(ptr, ret, val)                                    \
  do {                                                                        \
    *ret = __atomic_add_fetch(ptr, val, __ATOMIC_ACQ_REL);                    \
  }                                                                           \
  while (0)

UV_UNUSED(static int cmpxchgi(int* ptr, int oldval, int newval));
UV_UNUSED(static void cpu_relax(void));

/* Prefer hand-rolled assembly over the gcc builtins because the latter also
 * issue full memory barriers.
 */
UV_UNUSED(static int cmpxchgi(int* ptr, int oldval, int newval)) {
#if defined(__i386__) || defined(__x86_64__)
  int out;
  __asm__ __volatile__ ("lock; cmpxchg %2, %1;"
                        : "=a" (out), "+m" (*(volatile int*) ptr)
                        : "r" (newval), "0" (oldval)
                        : "memory");
  return out;
#elif defined(__MVS__)
  unsigned int op4;
  if (__plo_CSST(ptr, (unsigned int*) &oldval, newval,
                (unsigned int*) ptr, *ptr, &op4))
    return oldval;
  else
    return op4;
#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
  return atomic_cas_uint((uint_t *)ptr, (uint_t)oldval, (uint_t)newval);
#else
  return __sync_val_compare_and_swap(ptr, oldval, newval);
#endif
}

UV_UNUSED(static void cpu_relax(void)) {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__ ("rep; nop");  /* a.k.a. PAUSE */
#endif
}

#endif  /* UV_ATOMIC_OPS_H_ */
