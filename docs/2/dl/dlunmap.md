# dlunmap(2)

## SYNOPSIS

```c
long astia_dlunmap(uint64_t base);
```

## DESCRIPTION

Drop one dlmap handle for the image loaded at base.

The handle count is per process. While another handle remains the
mapping stays in place. The last handle unmaps every segment from the
calling process and releases its reference on the shared image; the
physical pages are returned when the final process mapping them lets go.

A process exit drops its remaining handles automatically.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EINVAL`: base is not a handle this process holds.
- `-ESRCH`: There is no current process.
