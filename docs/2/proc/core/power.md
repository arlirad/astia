# power(2)

## SYNOPSIS

```c
long astia_power(uint32_t operation);
```

## DESCRIPTION

Requests a whole-system power transition. The operation values are stable ABI:

- `ASTIA_POWER_SHUTDOWN` (0): enter the ordered kernel shutdown sequence and perform the platform ACPI S5 power-off as its final step.
- `ASTIA_POWER_REBOOT` (1): reset the machine through the validated ACPI FADT reset register.
- `ASTIA_POWER_SUSPEND` (2): enter whole-system sleep. This is not process or thread sleep; those use the scheduling and signal interfaces instead.

Only a process whose effective user ID is zero may call `astia_power`. The authorization check happens before any platform action. A successful shutdown or reset powers the machine off or resets it and does not return; if the platform action falls through or fails, the call reports the error rather than claiming success.

Shutdown uses the normal tiered shutdown sequence, so registered prepare, flush, quiesce, and platform handlers run before ACPI S5. The call returns only when shutdown dispatch fails or the platform does not power off.

Reboot requires a FADT reset register that passed validation: the FADT advertises the reset-register flag, the register is an 8-bit register with zero bit offset and a system-I/O or system-memory address, and its value is in range. ACPI also permits a bus-0 PCI Configuration reset register, but this kernel does not implement a native byte-write contract for that space and rejects it as unsupported rather than using a dword read-modify-write. There is no triple-fault, keyboard-controller, or unvalidated port-write fallback. Reboot reports `-ENOTSUP` when that path is unavailable.

Reboot uses the normal ordered shutdown sequence: prepare, flush, quiesce, and platform handlers run first, and the validated ACPI reset is the final action. A registered ACPI S5 final action is not run for a reboot. Handler failures are logged and do not prevent the final reset attempt.

Suspend has no implementation: the ACPI layer only parses `_S5`, and there is no S3 entry, wake vector, or resume path. The call returns `-ENOTSUP` and is never aliased to an ordinary sleep.

## RETURN VALUE

Returns zero only if a power transition was requested and reported success by its backend. Shutdown and reset normally do not return. Otherwise returns a negative errno.

## ERRORS

- `-EINVAL`: `operation` is not one of the documented values.
- `-ESRCH`: There is no current process.
- `-EPERM`: The effective user ID is not zero.
- `-ENOTSUP`: Shutdown, reboot, or suspend is not supported by the running platform.
- `-EIO`: The platform shutdown or reset sequence was dispatched but failed, or a reset write returned without resetting the machine.

## NOTES

The syscall performs no filesystem or device work of its own; shutdown and reboot delegate to the ACPI platform layer. On AArch64 the ACPI stub reports both shutdown and reset as unsupported.
