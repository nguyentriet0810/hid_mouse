import hashlib
import importlib.util
from pathlib import Path
import unittest

PROJECT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("make_release", PROJECT / "tools" / "make-release.py")
RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELEASE)


class ReleaseContract(unittest.TestCase):
    def test_manifest_digest_and_identity(self):
        image = b"firmware test fixture"
        fields = dict(line.split("=", 1) for line in RELEASE.manifest_text("1.2.3", image).splitlines())
        self.assertEqual(fields["board"], "esp32s3-n16r8")
        self.assertEqual(fields["size"], str(len(image)))
        self.assertEqual(fields["sha256"], hashlib.sha256(image).hexdigest())
        self.assertEqual(fields["asset"], RELEASE.ASSET)

    def test_reject_invalid_versions(self):
        for version in ("v1.2.3", "1.2", "01.2.3", "1.2.3-rc1", "4294967296.0.0"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                RELEASE.manifest_text(version, b"x")

    def test_reject_empty_or_oversized_images(self):
        with self.assertRaises(ValueError):
            RELEASE.manifest_text("1.2.3", b"")
        with self.assertRaises(ValueError):
            RELEASE.manifest_text("1.2.3", bytes(0x600001))

    def test_partition_alignment_and_bounds(self):
        previous_end = 0x9000
        rows = {}
        for line in (PROJECT / "partitions.csv").read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                name, kind, subtype, offset, size, *_ = [field.strip() for field in line.split(",")]
                start, length = int(offset, 0), int(size, 0)
                self.assertGreaterEqual(start, previous_end)
                self.assertEqual(start % (0x10000 if kind == "app" else 0x1000), 0)
                self.assertEqual(length % 0x1000, 0)
                self.assertLessEqual(start + length, 0x1000000)
                rows[name] = (kind, subtype, start, length)
                previous_end = start + length
        self.assertEqual(rows["ota_0"][3], 0x600000)
        self.assertEqual(rows["ota_1"][3], 0x600000)
        self.assertEqual(rows["otadata"][3], 0x2000)
        self.assertEqual(previous_end, 0x1000000)


if __name__ == "__main__":
    unittest.main()
