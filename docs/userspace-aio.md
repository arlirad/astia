# Userspace AIO rings

Astia's userspace I/O ABI is a shared submission/completion ring. Operations
that can block are represented by `OP_*` submission queue entries (SQEs), and
the kernel publishes completion queue entries (CQEs). The ring is single
producer and single reaper unless userspace adds its own synchronization.

## Submit-only kick

Userspace fills an SQE completely, publishes the SQ tail with release ordering,
and calls `astia_io_submit()` to kick the kernel. The kick submits a batch and
returns immediately; it is not a wait-for-completion operation. A device may
park the request, so the return value of the kick is not the operation result.
The CQE, whose `user_data` echoes the SQE tag, is the source of truth.

```c
astia_sq_publish(ring, count);
astia_io_submit(ring, count);

while (astia_cq_ready(ring) != 0) {
    astia_cqe *cqe = astia_cq_peek(ring);
    handle_completion(cqe->user_data, cqe->result);
    astia_cq_consume(ring, 1);
}
```

Successful CQE results are non-negative payloads such as bytes, a descriptor,
or a process ID. Negative results are `-errno`. SQE links and drains order
submissions, but they do not turn the submit syscall into a blocking call.

## `SIGAIO` and CQ draining

When CQEs become available, the owning process may receive `SIGAIO`. The signal
is a notification, not a completion payload and not a replacement for draining
the CQ. A handler or resumed event loop reads CQEs with acquire ordering until
the queue is empty, then advances the CQ head with release ordering. Consuming
at least one CQE acknowledges the ring's notification generation.

CQ draining costs no syscall. Userspace must drain promptly so the kernel can
reuse CQ slots, and must not have two independent reapers advance the same CQ
head. Registered files and buffers can be pinned once and selected by SQE
flags; otherwise each operation uses its ordinary descriptor and buffer
validation path.
