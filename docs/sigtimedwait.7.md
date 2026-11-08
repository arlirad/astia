# sigtimedwait(2)

`sigtimedwait(set, info, timeout)` consumes the lowest-numbered matching
pending signal from the calling thread or process. A non-null timeout is a
relative interval; invalid timespec values return `EINVAL`, and expiry returns
`-1` with `errno == EAGAIN`. A null timeout waits without a timer. Pending
signals outside `set` are not consumed. An unblocked unrelated signal may
interrupt the wait with `EINTR`.

The current implementation supports Astia's 32-bit signal set and one pending
bit per signal. It does not provide realtime queued-signal multiplicity,
`sigqueue` payloads, or `sigwaitinfo`; the AArch64 port also reports
`ENOTSUP` for positive timed waits until it has a timer source.
