# `/dev/ttv0`

`/dev/ttv0` is a first-class virtual terminal device. It has its own tty line
discipline state, input queue, readiness notifications, termios, and
controlling-terminal and foreground-process-group state. Opening it creates a
new open-file description, while the terminal state is shared by opens of the
same device as it is for a physical tty.

The device is deliberately not an alias for `/dev/tts0` or `/dev/console`.
`/dev/tts0` remains the UART tty, `/dev/console` remains the explicitly
registered console file, and `/dev/ttv0` is a separate tty whose output is sent
through the active console backend. This keeps standard streams and UART
behavior unchanged while making console selection explicit.

The backend contract is byte-oriented. The current backend is the existing
UART console sink, because this target has no framebuffer hardware or
framebuffer driver. The contract is also the extension point for a future
framebuffer text renderer; `/dev/ttv0` currently does not claim to provide
pixel or framebuffer support.

If no active console backend is registered, virtual-terminal initialization
fails and writes do not report the device as writable.
