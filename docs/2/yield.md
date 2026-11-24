# yield(2)

## SYNOPSIS

```c
long astia_yield(void);
```

## DESCRIPTION

Yield the current scheduling turn.

Gives another runnable thread an opportunity to run. The caller
remains runnable and may be selected again immediately.

## RETURN VALUE

Returns zero.
