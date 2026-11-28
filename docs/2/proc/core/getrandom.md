# getrandom(2)

## SYNOPSIS

```c
long astia_getrandom(void* buffer, uint64_t length, uint32_t flags);
```

## DESCRIPTION

Fills `length` bytes from the kernel CSPRNG. The pool seeds on the first
request from available platform entropy and then never blocks, so this call
does not have an `EINTR` path. Only a strong source seeds the pool: a hardware
RNG, a bootloader-provided seed, or a platform clock. Cycle-counter jitter,
timer ticks, and addresses are mixed as weak material but never make the pool
ready on their own, so an AArch64 platform without a hardware RNG or loader
seed reports `EAGAIN` instead of returning predictable bytes. Only `flags == 0`
is accepted; the Linux `GRND_*` flags have no meaning while the pool cannot
block.

`length` is capped at one mebibyte; larger requests are rejected rather than
truncated. A zero-length request succeeds without touching `buffer`.

`/dev/urandom` exposes the same stream. `/dev/random` is intentionally not
created: there is no blocking pool policy to give it distinct semantics.

## RETURN VALUE

Returns the number of bytes written (`length`), or a negative errno.

## ERRORS

- `-EAGAIN`: The platform has no usable entropy source, so the pool is not seeded.
- `-EFAULT`: `buffer` is null or not writable for `length` bytes.
- `-EINVAL`: `flags` is nonzero or `length` exceeds the documented maximum.
- `-ESRCH`: There is no current process.
