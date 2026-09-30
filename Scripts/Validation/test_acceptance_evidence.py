#!/usr/bin/env python3
"""Focused acceptance-ledger regression tests; no builds, applications, or repository mutations."""

import argparse
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import acceptance_evidence as evidence


class AcceptanceEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "ledger.json"
        self.artifact = self.root / "result.log"
        self.artifact.write_text("actual retained test output\n", encoding="utf-8")
        self.source = {"revision": "a" * 40, "workingTreeSha256": "b" * 64, "dirty": False}
        self.ledger = {"schemaVersion": 1, "source": self.source, "targets": ["windows-x86_64"], "records": []}

    def arguments(self, **changes):
        values = dict(target="windows-x86_64", lane="core-tests", result="pass", mode="native",
                      artifact=[str(self.artifact)], notes="", backend="", toolchain="MSVC fixture",
                      command="fixture test command")
        values.update(changes)
        return argparse.Namespace(**values)

    def record(self, **changes):
        with patch.object(evidence, "host_target", return_value="windows-x86_64"):
            return evidence.make_record(self.arguments(**changes), self.ledger, self.path, self.source)

    def test_pass_requires_artifacts_and_native_host_identity(self):
        with self.assertRaisesRegex(ValueError, "artifacts"):
            self.record(artifact=[])
        with patch.object(evidence, "host_target", return_value="linux-x86_64"):
            with self.assertRaisesRegex(ValueError, "Native"):
                evidence.make_record(self.arguments(), self.ledger, self.path, self.source)

    def test_unavailable_and_render_require_explanations(self):
        with self.assertRaisesRegex(ValueError, "--notes"):
            self.record(result="unavailable", artifact=[])
        self.assertEqual(self.record(result="unavailable", artifact=[], notes="No native host")["result"], "unavailable")
        with self.assertRaisesRegex(ValueError, "--backend"):
            self.record(lane="render")

    def test_changed_source_cannot_receive_old_results(self):
        changed = {**self.source, "workingTreeSha256": "c" * 64}
        with self.assertRaisesRegex(ValueError, "differs"):
            evidence.make_record(self.arguments(), self.ledger, self.path, changed)

    def test_artifact_tampering_and_removal_are_rejected(self):
        record = self.record()
        self.assertTrue(evidence.verify_artifacts(record, self.path))
        self.artifact.write_text("different output", encoding="utf-8")
        self.assertFalse(evidence.verify_artifacts(record, self.path))
        self.artifact.unlink()
        self.assertFalse(evidence.verify_artifacts(record, self.path))

    def test_matrix_requires_every_native_lane_and_current_source(self):
        self.assertFalse(evidence.summarize(self.ledger, self.path, self.source)[1])
        self.ledger["records"] = [self.record(lane=lane, backend="d3d12" if lane == "render" else "")
                                  for lane in evidence.LANES]
        self.assertTrue(evidence.summarize(self.ledger, self.path, self.source)[1])
        other = {**self.source, "revision": "c" * 40}
        self.assertFalse(evidence.summarize(self.ledger, self.path, other)[1])
        for changes in ({"mode": "cross"}, {"result": "fail"}, {"result": "unavailable"}):
            ledger = copy.deepcopy(self.ledger)
            ledger["records"][-1].update(changes)
            self.assertFalse(evidence.summarize(ledger, self.path, self.source)[1])

    def test_latest_failure_keeps_previous_evidence_and_blocks_acceptance(self):
        self.ledger["records"] = [self.record(lane=lane, backend="d3d12" if lane == "render" else "")
                                  for lane in evidence.LANES]
        self.ledger["records"].append(self.record(result="fail"))
        lines, complete = evidence.summarize(self.ledger, self.path, self.source)
        self.assertFalse(complete)
        self.assertIn("fail (native)", "\n".join(lines))
        self.assertEqual(len(self.ledger["records"]), len(evidence.LANES) + 1)

    def test_atomic_ledger_write_and_competing_writer_rejection(self):
        with evidence.ledger_lock(self.path):
            with self.assertRaisesRegex(ValueError, "being written"):
                with evidence.ledger_lock(self.path):
                    self.fail("concurrent writer acquired the lock")
            evidence.write_ledger(self.path, self.ledger)
        self.assertEqual(evidence.read_ledger(self.path), self.ledger)
        self.assertFalse(self.path.with_suffix(".json.lock").exists())

    def test_fingerprint_includes_untracked_content_and_rejects_mid_capture_changes(self):
        source_file = self.root / "source.cpp"
        source_file.write_text("first", encoding="utf-8")

        def git_result(root, *arguments):
            if arguments[0] == "rev-parse":
                return b"a" * 40 + b"\n"
            if arguments[0] == "status":
                return b"?? source.cpp\0"
            if arguments[0] == "ls-files":
                return b"source.cpp\0"
            return b""

        with patch.object(evidence, "git", side_effect=git_result):
            first = evidence.snapshot(self.root)
            source_file.write_text("second", encoding="utf-8")
            second = evidence.snapshot(self.root)
        self.assertNotEqual(first["workingTreeSha256"], second["workingTreeSha256"])
        with patch.object(evidence, "git", side_effect=[b"a" * 40, b"", b"first diff", b"", b"a" * 40, b"", b"changed diff"]):
            with self.assertRaisesRegex(ValueError, "changed"):
                evidence.snapshot(self.root)


if __name__ == "__main__":
    unittest.main()
