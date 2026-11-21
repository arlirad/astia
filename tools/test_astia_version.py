import json
import tempfile
import unittest
from pathlib import Path

from astia_version import VersionError, derive, main


def metadata(**updates):
    data = {
        "sha": "0123456789abcdef0123456789abcdef01234567",
        "branch": "main",
        "dirty": False,
        "shallow": False,
        "commits": 4,
        "tags": [],
    }
    data.update(updates)
    return data


class VersionTests(unittest.TestCase):
    def test_no_tag(self):
        self.assertEqual(derive(metadata())["version"], "0.0.0-dev.4+0123456789ab")

    def test_release_tag(self):
        version = derive(metadata(tags=[{"tag": "v2.4.6", "distance": 0}]))
        self.assertEqual(version["version"], "2.4.6")
        self.assertTrue(version["release"])

    def test_post_tag_and_branch_marker(self):
        version = derive(
            metadata(branch="feature/new-ui", tags=[{"tag": "v2.4.6", "distance": 3}])
        )
        self.assertEqual(version["version"], "2.4.6-feature-new-ui.3+0123456789ab")

    def test_dirty_marked_and_release_rejection(self):
        version = derive(metadata(dirty=True, tags=[{"tag": "v1.2.3", "distance": 0}]))
        self.assertEqual(version["version"], "1.2.3-dev.0+0123456789ab.dirty")
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "metadata.json"
            output = Path(directory) / "version.json"
            fixture.write_text(json.dumps(metadata(dirty=True)))
            import sys

            old = sys.argv
            try:
                sys.argv = [
                    "astia_version.py",
                    "--metadata-json",
                    str(fixture),
                    "--output",
                    str(output),
                    "--reject-dirty",
                ]
                self.assertEqual(main(), 1)
            finally:
                sys.argv = old

    def test_override_is_reproducible_and_offline(self):
        result = derive({}, "7.8.9")
        self.assertEqual(result["version"], "7.8.9")
        self.assertTrue(result["override"])
        with self.assertRaises(VersionError):
            derive({}, "v7.8")

    def test_malformed_release_tag(self):
        with self.assertRaisesRegex(VersionError, "malformed release tag"):
            derive(metadata(tags=[{"tag": "v1.2", "distance": 0}]))

    def test_detached_sha_and_shallow_clone(self):
        fixture = metadata(branch="", shallow=True, commits=1)
        version = derive(fixture)
        self.assertEqual(version["version"], "0.0.0-dev.1+0123456789ab")
        self.assertTrue(version["shallow"])
        self.assertEqual(version["sha"], fixture["sha"])


if __name__ == "__main__":
    unittest.main()
