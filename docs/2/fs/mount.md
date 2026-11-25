# mount(2)

## SYNOPSIS

```c
long astia_mount(const char* type, const char* target, const char* source);
```

## DESCRIPTION

Install a registered native filesystem at an existing directory.

All names are copied before authorization.  The operation is root-only;
it changes only the in-memory VFS namespace and never opens a host path.

## RETURN VALUE

Returns zero on success, or a negative errno.
