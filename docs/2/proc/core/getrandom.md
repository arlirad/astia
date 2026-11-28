# getrandom(2)

## SYNOPSIS

```c
long astia_getrandom(void* buffer, uint64_t length, uint32_t flags);
```

## DESCRIPTION

Fills `length` bytes from the kernel CSPRNG. The pool seeds on the first
request from available platform entropy and then never blocks, so this call
does not have an `EINTR` path. Only `flags == 0` is accepted; the Linux
`GRND_*` flags have no meaning while the pool cannot block.

`length` is capped at one mebibyte; larger requests are rejected rather than
truncated. A zero-length request succeeds without touching `buffer`.

The call copies the result in chunks. A failure after the first chunk has
been copied stops the call and returns the bytes written so far, so a
positive return can be shorter than `length`. Callers that require an exact
byte count must retry with the remaining buffer.

`/dev/urandom` exposes the same stream. `/dev/random` is intentionally not
created: there is no blocking pool policy to give it distinct semantics.

## RETURN VALUE

Returns the number of bytes written, or a negative errno. A successful
return is usually `length`, but may be a shorter positive count when a
failure occurs after part of the request has been copied.

## ERRORS

- `-EAGAIN`: The platform has no usable entropy source, so the pool is not seeded.
- `-EFAULT`: `buffer` is null or not writable for `length` bytes. If a chunk copy fails after earlier chunks were written, the call instead returns the positive count already copied.
- `-EINVAL`: `flags` is nonzero or `length` exceeds the documented maximum.
- `-ESRCH`: There is no current process.
