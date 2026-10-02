"""Guard against false positives in the real-emulator recovery verifier."""
import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "recovery", Path(__file__).with_name("test-nsmb-recovery.py"))
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class RecoveryVerifierTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        for role in ("host", "client"):
            folder = self.output / role
            folder.mkdir()
            (folder / "stdout.txt").write_text(
                "NSMB Recovery: resumed\nframe limit reached\n", encoding="utf-8")
            self.write_trace(role, range(1000, 1900))

    def write_trace(self, role, frames, mismatch=None):
        with (self.output / role / "game-tick-g0.csv").open("w", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=["frame", *recovery.FIELDS])
            writer.writeheader()
            for frame in frames:
                row = dict.fromkeys(recovery.FIELDS, "0")
                row["frame"] = frame
                if frame == mismatch:
                    row["playerActor0X"] = "1"
                writer.writerow(row)

    def verify(self):
        return recovery.verify(self.output, False, True,
                               [{"generation": 0, "frame": 1500}])["ok"]

    def test_matching_recovery(self):
        self.assertTrue(self.verify())

    def test_sparse_replay_gates(self):
        self.write_trace("host", [f for f in range(1000, 1900) if f != 1530])
        self.assertTrue(self.verify())

    def test_startup_only_cannot_pass(self):
        self.write_trace("client", range(1000, 1300))
        self.assertFalse(self.verify())

    def test_post_recovery_mismatch(self):
        self.write_trace("client", range(1000, 1900), mismatch=1600)
        self.assertFalse(self.verify())

    def test_duplicate_frame_rejected(self):
        self.write_trace("host", [*range(1000, 1900), 1899])
        with self.assertRaises(AssertionError):
            self.verify()


if __name__ == "__main__":
    unittest.main()
