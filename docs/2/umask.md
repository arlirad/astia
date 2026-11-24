# umask(2)

## SYNOPSIS

```c
long astia_umask(uint32_t mode);
```

## DESCRIPTION

Sets the calling process's file-creation permission mask.

## RETURN VALUE

Returns the previous mask.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

Only permission bits represented by the filesystem mode are retained.
