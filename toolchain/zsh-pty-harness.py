#!/usr/bin/env python3
import argparse
import os
import re
import selectors
import signal
import subprocess
import sys
import termios
import time


PROMPT = b"ASTIA_READY> "
EXPECTED_STATUS = 1
TIMEOUT = 60.0
KERNEL_PROC_LINE = re.compile(rb"^\[\s*[0-9]+\.[0-9]+[^\r\n]*\]\s+proc:")


class HarnessError(RuntimeError):
    pass


def trace(stream, direction, data):
    stream.write(f"{time.monotonic_ns()} {direction} {len(data)} ".encode())
    stream.write(data.hex().encode())
    stream.write(b"\n")
    stream.flush()


def raw(fd):
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def wait_serial_pty(process, deadline):
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    text = b""
    while time.monotonic() < deadline:
        for key, _ in selector.select(max(0.05, deadline - time.monotonic())):
            data = os.read(key.fd, 4096)
            if not data:
                break
            text += data
            match = re.search(rb"(/dev/pts/[0-9]+)", text)
            if match:
                return match.group(1).decode()
    raise HarnessError("qemu did not publish its serial PTY: " + text.decode(errors="replace"))


class Serial:
    def __init__(self, fd, trace_stream):
        self.fd = fd
        self.trace_stream = trace_stream
        self.selector = selectors.DefaultSelector()
        self.selector.register(fd, selectors.EVENT_READ)
        self.buffer = bytearray()

    def send(self, data):
        view = memoryview(data)
        while view:
            written = os.write(self.fd, view)
            view = view[written:]
        trace(self.trace_stream, "TX", data)

    def read_until(self, needle, deadline):
        while True:
            index = self.buffer.find(needle)
            if index >= 0:
                result = bytes(self.buffer[:index])
                del self.buffer[:index + len(needle)]
                return result
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise HarnessError("timed out waiting for prompt")
            events = self.selector.select(remaining)
            if not events:
                raise HarnessError("timed out waiting for serial data")
            for key, _ in events:
                data = os.read(key.fd, 4096)
                if not data:
                    raise HarnessError("serial PTY closed before prompt")
                trace(self.trace_stream, "RX", data)
                self.buffer.extend(data)


def shell_output(response, command):
    if b"zsh:" in response or b"command not found" in response:
        raise HarnessError("shell reported an error for %r: %r" % (command, response))
    response = response.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    lines = response.splitlines(keepends=True)
    output = bytearray()
    for line in lines:
        content = line[:-1] if line.endswith(b"\n") else line
        if KERNEL_PROC_LINE.match(content):
            continue
        if content == command.encode() or content == b"":
            continue
        if content.startswith(b"#") and content[1:].strip(b" ") == b"":
            continue
        if content.strip(b" ") == b"":
            continue
        output.extend(line)
    return bytes(output)


def run_command(serial, command, expected):
    serial.send(command.encode() + b"\n")
    response = shell_output(serial.read_until(PROMPT, time.monotonic() + TIMEOUT), command)
    wanted = expected.encode()
    if response != wanted:
        raise HarnessError(
            "unexpected output for %r: expected %r, got %r" %
            (command, wanted, response)
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--initrd", required=True)
    parser.add_argument("--trace", required=True)
    args = parser.parse_args()

    with open(args.trace, "wb", buffering=0) as trace_stream:
        command = [
            args.qemu, "-m", "128M", "-kernel", args.kernel,
            "-initrd", args.initrd, "-serial", "pty", "-display", "none",
            "-monitor", "none", "-device", "isa-debug-exit", "-no-reboot",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
        serial_fd = None
        try:
            serial_path = wait_serial_pty(process, time.monotonic() + TIMEOUT)
            serial_fd = os.open(serial_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            raw(serial_fd)
            serial = Serial(serial_fd, trace_stream)
            serial.read_until(PROMPT, time.monotonic() + TIMEOUT)
            run_command(serial, "echo TEST_OK", "TEST_OK\n")
            run_command(serial, "pwd", "/\n")
            run_command(serial, "echo PIPE_OK | cat", "PIPE_OK\n")
            run_command(serial, "echo REDIRECT_OK >/tmp/zsh-output; cat /tmp/zsh-output",
                        "REDIRECT_OK\n")
            serial.send(b"\x04")
            trace(trace_stream, "WAIT", b"eof")
            try:
                status = process.wait(timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                raise HarnessError("qemu did not exit after zsh exit")
            if status != EXPECTED_STATUS:
                raise HarnessError("unexpected qemu status %d" % status)
        except Exception:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            process.wait(timeout=5)
            raise
        finally:
            if serial_fd is not None:
                os.close(serial_fd)
            if process.poll() is None:
                process.kill()
                process.wait()
    print("zsh PTY: PASS")


if __name__ == "__main__":
    try:
        main()
    except HarnessError as error:
        print("zsh PTY: FAIL: %s" % error, file=sys.stderr)
        sys.exit(1)
