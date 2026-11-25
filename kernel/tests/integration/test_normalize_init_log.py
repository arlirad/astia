#!/usr/bin/env python3
"""Unit tests for the init lifecycle log reassembler."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from normalize_init_log import normalize


class NormalizeInitLogTest(unittest.TestCase):
    def test_foreign_record_interleaved_mid_marker(self):
        text = (
            "[ 0.100000  INFO] core: init "
            "[ 0.100100  INFO] proc: interleaved\n"
            "launched pid=1\n"
        )
        output = normalize(text)
        self.assertIn("core: init launched pid=1\n", output)
        self.assertEqual(output.count("init launched pid="), 1)
        self.assertNotIn("proc: interleaved", output)

    def test_plain_newline_split_without_foreign_record(self):
        text = (
            "[ 0.100000  INFO] core: init \n"
            "launched pid=1\n"
            "[ 0.100100  INFO] core: selftest ok\n"
        )
        output = normalize(text)
        self.assertIn("core: init launched pid=1\n", output)
        self.assertNotIn("core: init \n", output)
        self.assertNotIn("selftest", output)

    def test_unsplit_log_keeps_order(self):
        text = (
            "[ 1.000000  INFO] core: init launched pid=1\n"
            "[ 1.000100 ERROR] core: init exited pid=1 status=0 wait=0\n"
            "[ 1.000200 PANIC] core: init terminated\n"
        )
        output = normalize(text)
        self.assertEqual(
            output.splitlines(),
            [
                "[ 1.000000  INFO] core: init launched pid=1",
                "[ 1.000100 ERROR] core: init exited pid=1 status=0 wait=0",
                "[ 1.000200 PANIC] core: init terminated",
            ],
        )

    def test_marker_split_across_two_foreign_records(self):
        text = (
            "[ 0.100000  INFO] core: init "
            "[ 0.100100  INFO] proc: first\n"
            "launched "
            "[ 0.100200  INFO] proc: second\n"
            "pid=1\n"
            "[ 0.100300 PANIC] core: init terminated\n"
        )
        output = normalize(text)
        self.assertIn("core: init launched pid=1\n", output)
        self.assertIn("core: init terminated\n", output)
        self.assertEqual(output.count("init launched pid="), 1)
        self.assertEqual(output.count("pid=1"), 1)
        self.assertNotIn("proc: first", output)
        self.assertNotIn("proc: second", output)

    def test_text_without_prefix_passes_through(self):
        text = "no kernel records here\n"
        self.assertEqual(normalize(text), text)


if __name__ == "__main__":
    unittest.main()
