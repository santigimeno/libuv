
.. _metrics:

Metrics operations
======================

libuv provides a metrics API to track the amount of time the event loop has
spent idle in the kernel's event provider.


API
---

.. c:function:: uint64_t uv_metrics_idle_time(uv_loop_t* loop)

    Retrieve the amount of time the event loop has been idly waiting in the
    kernel's event provider (e.g. ``epoll_wait``).

    The return value is the accumulated time spent idle in the kernel's event
    provider for the entire lifespan of the :c:type:`uv_loop_t`.
