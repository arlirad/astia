from __future__ import annotations

import os
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

ENTRYPOINT = Path(__file__).resolve().parents[1] / "entrypoint.sh"


class EntrypointTests(unittest.TestCase):
    def run_entrypoint(self, socket_path: Path, work: Path):
        fake_bin = work / "bin"
        fake_bin.mkdir()
        (fake_bin / "dotnet").write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$@" > "$FAKE_DOTNET_LOG"\n'
            'test ! -e "$PKGSRV_TEST_SOCKET"\n'
        )
        (fake_bin / "dotnet").chmod(0o755)
        log = work / "dotnet.log"
        env = {
            "PATH": f"{fake_bin}:/usr/bin:/bin",
            "Pkgsrv__Listen__UnixSocketPath": str(socket_path),
            "FAKE_DOTNET_LOG": str(log),
            "PKGSRV_TEST_SOCKET": str(socket_path),
        }
        return subprocess.run(
            [str(ENTRYPOINT)],
            env=env,
            capture_output=True,
            text=True,
        ), log

    def test_removes_owned_stale_socket(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            sock = work / "pkgsrv.sock"
            server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            server.bind(str(sock))
            try:
                result, log = self.run_entrypoint(sock, work)
            finally:
                server.close()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(sock.exists())
            self.assertIn("/app/Pkgsrv.Presentation.dll", log.read_text())

    def test_refuses_to_delete_regular_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            sock = work / "pkgsrv.sock"
            sock.write_text("not a socket")
            result, log = self.run_entrypoint(sock, work)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(sock.exists())
            self.assertFalse(log.exists())

    def test_refuses_to_delete_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            sock = work / "pkgsrv.sock"
            sock.mkdir()
            result, log = self.run_entrypoint(sock, work)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(sock.is_dir())
            self.assertFalse(log.exists())


if __name__ == "__main__":
    unittest.main()
