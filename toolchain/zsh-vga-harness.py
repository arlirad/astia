#!/usr/bin/env python3
import argparse
import os
import re
import selectors
import signal
import socket
import subprocess
import sys
import termios
import time


TIMEOUT = 60.0
SERIAL_PTY = re.compile(rb"(/dev/pts/[0-9]+)")
PROMPT = b"ASTIA_READY> "
TTY_ERRORS = (b"bad tcgets", b"unknown error")


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


class Serial:
    def __init__(self, fd, log, trace_stream):
        self.fd = fd
        self.log = log
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

    def pump(self, timeout):
        events = self.selector.select(timeout)
        for key, _ in events:
            data = os.read(key.fd, 4096)
            if not data:
                raise HarnessError("serial PTY closed")
            self.buffer.extend(data)
            self.log.write(data)
            self.log.flush()
            trace(self.trace_stream, "RX", data)
        return bool(events)

    def read_until(self, needle, deadline):
        while True:
            index = self.buffer.find(needle)
            if index >= 0:
                result = bytes(self.buffer[:index])
                del self.buffer[:index + len(needle)]
                return result
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise HarnessError("timed out waiting for %r" % needle)
            self.pump(min(0.2, remaining))

    def drain(self):
        while self.pump(0):
            pass


def wait_serial_pty(process, deadline, output):
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    while time.monotonic() < deadline:
        events = selector.select(min(0.2, deadline - time.monotonic()))
        for key, _ in events:
            data = os.read(key.fd, 4096)
            if not data:
                break
            output.extend(data)
            match = SERIAL_PTY.search(output)
            if match:
                return match.group(1).decode()
    raise HarnessError("qemu did not publish its serial PTY: " + output.decode(errors="replace"))


def wait_socket(path, deadline):
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.05)
    raise HarnessError("qemu did not publish its monitor socket")


def monitor_command(path, command):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
                monitor.settimeout(1)
                monitor.connect(path)
                response = bytearray()
                while b"(qemu)" not in response:
                    response.extend(monitor.recv(4096))
                monitor.sendall(command.encode() + b"\n")
                response = bytearray()
                while time.monotonic() < deadline:
                    try:
                        part = monitor.recv(4096)
                    except socket.timeout:
                        continue
                    if not part:
                        break
                    response.extend(part)
                    if b"(qemu)" in response:
                        break
                return bytes(response)
        except OSError:
            time.sleep(0.05)
    raise HarnessError("could not issue QEMU monitor command: " + command)


def screenshot(monitor, path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    response = monitor_command(monitor, "screendump " + path)
    if b"unknown command" in response or b"Error" in response:
        raise HarnessError("QEMU screendump failed: " + response.decode(errors="replace"))
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if os.path.isfile(path) and os.path.getsize(path) > 0:
            return
        time.sleep(0.05)
    raise HarnessError("QEMU did not create framebuffer screenshot")


def screen_text(path):
    converted = path + ".ocr.png"
    try:
        result = subprocess.run(
            ["convert", path, "-resize", "200%", "-filter", "point", converted],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            raise HarnessError("ImageMagick resize failed: " + result.stderr.decode(errors="replace"))
        result = subprocess.run(
            ["tesseract", converted, "stdout", "--psm", "6"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            raise HarnessError("tesseract failed: " + result.stderr.decode(errors="replace"))
        return result.stdout.decode(errors="replace")
    finally:
        try:
            os.unlink(converted)
        except FileNotFoundError:
            pass


def screen_has_green(path):
    result = subprocess.run(
        ["convert", path, "-format", "%c", "histogram:info:-"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise HarnessError("ImageMagick histogram failed: " + result.stderr.decode(errors="replace"))
    return bool(re.search(rb"#(?:20E020|00AA00|00aA00)", result.stdout))


def screen_geometry(path):
    result = subprocess.run(
        ["identify", "-format", "%wx%h", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise HarnessError("ImageMagick identify failed: " + result.stderr.decode(errors="replace"))
    return result.stdout.decode(errors="replace")


def wait_screen(monitor, path, required, green=False):
    deadline = time.monotonic() + TIMEOUT
    last_text = ""
    while time.monotonic() < deadline:
        screenshot(monitor, path)
        last_text = screen_text(path)
        if all(item.lower() in last_text.lower() for item in required) and (not green or screen_has_green(path)):
            return last_text
        time.sleep(0.25)
    raise HarnessError("screen did not contain %r; OCR was %r" % (required, last_text))


def run_serial_command(serial, command, expected):
    serial.send(command.encode() + b"\n")
    response = serial.read_until(PROMPT, time.monotonic() + TIMEOUT)
    if any(marker in response for marker in TTY_ERRORS):
        raise HarnessError("terminal error after %r: %r" % (command, response))
    normalized = response.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    if expected not in normalized:
        raise HarnessError("expected %r after %r, got %r" % (expected, command, response))


def start(args, trace_stream, serial_log):
    monitor = args.serial_log + ".monitor"
    try:
        os.unlink(monitor)
    except FileNotFoundError:
        pass
    command = [
        args.qemu,
        "-smp",
        str(args.cpus),
        "-m",
        args.memory,
        "-machine",
        "q35",
        "-vga",
        "std",
    ]
    if args.kernel is None:
        command.extend(["-cdrom", args.iso])
    else:
        command.extend(["-kernel", args.kernel, "-initrd", args.initrd])
    command.extend([
        "-serial",
        "pty",
        "-display",
        args.display,
        "-monitor",
        "unix:%s,server=on,wait=off" % monitor,
        "-no-reboot",
        "-no-shutdown",
    ])
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        close_fds=True,
    )
    qemu_output = bytearray()
    try:
        serial_path = wait_serial_pty(process, time.monotonic() + TIMEOUT, qemu_output)
        serial_fd = os.open(serial_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        raw(serial_fd)
        wait_socket(monitor, time.monotonic() + TIMEOUT)
        serial = Serial(serial_fd, serial_log, trace_stream)
        return process, serial_fd, serial, monitor, qemu_output
    except Exception:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
        raise


def verify_boot(serial, expected, strict):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        serial.pump(min(0.2, deadline - time.monotonic()))
        data = bytes(serial.buffer)
        if b"framebuffer: failed" in data:
            raise HarnessError("kernel reported framebuffer: failed")
        if expected == "ready" and b"framebuffer: unavailable" in data:
            raise HarnessError("host GRUB/QEMU emitted no framebuffer descriptor; use ASTIA_VGA_FALLBACK=serial explicitly")
        if expected == "unavailable" and b"framebuffer: ready" in data:
            raise HarnessError("serial fallback expected framebuffer: unavailable, but the bootloader emitted a descriptor")
        marker = b"framebuffer: ready" if expected == "ready" else b"framebuffer: unavailable"
        if marker in data:
            if expected == "ready" and b"ttv0: selected" not in data:
                continue
            if strict and expected == "ready":
                required = b"boot framebuffer: paddr="
                if required not in data or b"800x600 pitch=2400 format=1" not in data:
                    raise HarnessError("strict VGA requires GRUB's 800x600x24 BGR framebuffer descriptor")
            return
    raise HarnessError("did not observe framebuffer marker %s" % expected)


def run_ready_smoke(serial, monitor, work, args):
    image = os.path.join(work, "framebuffer.png")
    if args.line_reader:
        text = wait_screen(monitor, image, ["LINE_READY"])
        serial.send(b"exit\n")
        wait_screen(monitor, image, ["LINE_BYTES", "exit"])
        return text
    wait_screen(monitor, image, ["ASTIA_VGA_READY"], green=True)
    if args.strict and screen_geometry(image) != "800x600":
        raise HarnessError("strict VGA screenshot geometry is not 800x600")
    if args.strict:
        return
    serial.send(b"echo VGA_TEST_OK\n")
    wait_screen(monitor, image, ["VGA_TEST_OK", "ASTIA_VGA_READY"])
    serial.send(b"pwd\n")
    wait_screen(monitor, image, ["VGA_TEST_OK", "/"])
    if args.nano:
        serial.send(b"nano\n")
        wait_screen(monitor, image, ["GNU nano"])
        serial.send(b"\x18")
        wait_screen(monitor, image, ["ASTIA_VGA_READY"])
        for _ in range(args.nano_iterations):
            serial.send(b"true\n")
            wait_screen(monitor, image, ["ASTIA_VGA_READY"])
    serial.send(b"exit\n")


def run_fallback_smoke(serial, args):
    if args.line_reader:
        serial.read_until(b"LINE_READY> ", time.monotonic() + TIMEOUT)
        serial.send(b"exit\n")
        serial.read_until(b"LINE_BYTES: exit\n", time.monotonic() + TIMEOUT)
        return
    serial.read_until(PROMPT, time.monotonic() + TIMEOUT)
    run_serial_command(serial, "echo VGA_TEST_OK", b"VGA_TEST_OK")
    run_serial_command(serial, "pwd", b"/\n")
    if args.line_reader:
        serial.send(b"exit\n")
    else:
        serial.send(b"exit\n")


def interactive(serial_fd, serial, process, args):
    selector = selectors.DefaultSelector()
    selector.register(sys.stdin, selectors.EVENT_READ)
    selector.register(serial_fd, selectors.EVENT_READ)
    while process.poll() is None:
        for key, _ in selector.select(0.2):
            if key.fileobj is sys.stdin:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data:
                    return
                serial.send(data)
            else:
                data = os.read(serial_fd, 4096)
                if not data:
                    return
                serial.log.write(data)
                serial.log.flush()
                trace(serial.trace_stream, "RX", data)
                if args.serial_shell:
                    os.write(sys.stdout.fileno(), data)
                else:
                    os.write(sys.stderr.fileno(), data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--iso")
    parser.add_argument("--kernel")
    parser.add_argument("--initrd")
    parser.add_argument("--display", required=True)
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--memory", default="128M")
    parser.add_argument("--expected-framebuffer", choices=("ready", "unavailable"), required=True)
    parser.add_argument("--serial-log", required=True)
    parser.add_argument("--trace", required=True)
    parser.add_argument("--strict", action="store_true")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--smoke", action="store_true")
    mode.add_argument("--interactive", action="store_true")
    parser.add_argument("--serial-shell", action="store_true")
    parser.add_argument("--line-reader", action="store_true")
    parser.add_argument("--nano", action="store_true")
    parser.add_argument("--nano-iterations", type=int, default=1)
    args = parser.parse_args()
    if (args.kernel is None) != (args.initrd is None):
        raise HarnessError("--kernel and --initrd must be supplied together")
    if args.iso is None and args.kernel is None:
        raise HarnessError("either --iso or --kernel/--initrd is required")
    if args.nano_iterations < 1:
        raise HarnessError("--nano-iterations must be positive")
    for program in ("convert", "identify", "tesseract") if args.smoke and args.expected_framebuffer == "ready" else ():
        if subprocess.call(["sh", "-c", "command -v %s >/dev/null 2>&1" % program]) != 0:
            raise HarnessError("%s is required for framebuffer smoke capture" % program)
    os.makedirs(os.path.dirname(args.serial_log), exist_ok=True)
    os.makedirs(os.path.dirname(args.trace), exist_ok=True)
    with open(args.trace, "wb", buffering=0) as trace_stream, open(args.serial_log, "wb", buffering=0) as serial_log:
        process = None
        serial_fd = None
        try:
            process, serial_fd, serial, monitor, qemu_output = start(args, trace_stream, serial_log)
            serial_log.write(qemu_output)
            serial_log.flush()
            verify_boot(serial, args.expected_framebuffer, args.strict)
            if args.smoke:
                if args.expected_framebuffer == "ready":
                    run_ready_smoke(serial, monitor, os.path.dirname(args.serial_log), args)
                else:
                    run_fallback_smoke(serial, args)
                print("zsh VGA smoke: PASS")
            else:
                interactive(serial_fd, serial, process, args)
        except Exception:
            if process is not None and process.poll() is None:
                process.send_signal(signal.SIGTERM)
            if process is not None:
                process.wait(timeout=5)
            raise
        finally:
            if serial_fd is not None:
                os.close(serial_fd)
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            try:
                os.unlink(args.serial_log + ".monitor")
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    try:
        main()
    except HarnessError as error:
        print("zsh VGA: FAIL: %s" % error, file=sys.stderr)
        sys.exit(1)
