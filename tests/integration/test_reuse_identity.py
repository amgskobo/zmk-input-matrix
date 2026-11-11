# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Completed-binary reuse rejects stale compiled inputs and producer identities."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ENTRY = Path(__file__).resolve().parents[1] / "deep-scan/source_identity.py"
SPEC = importlib.util.spec_from_file_location("matrix_source_identity", ENTRY)
IDENTITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IDENTITY)


class ReuseIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="matrix-reuse-")
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.root, self.owner, self.iqs, self.workspace = [base / name for name in ("root", "ownership", "iqs", "workspace")]
        for folder in (self.root / "src", self.owner, self.iqs / "src", self.workspace):
            folder.mkdir(parents=True)
        self.source = self.root / "src/input_processor_matrix.c"
        self.source.write_text("synthetic compiled Matrix input\n")
        self.producer = self.iqs / "src/iqs7211e.c"
        self.producer.write_text("synthetic compiled IQS input\n")
        self.compiled = {"src/input_processor_matrix.c": self.digest(self.source)}
        self.commit = "1" * 40
        self.dependencies = {"zmk": {"revision": "2" * 40}, "zephyr": {"revision": "3" * 40}}
        (self.owner / "source-after.json").write_text(json.dumps(self.compiled))
        (self.owner / "iqs-after.json").write_text(json.dumps(dict(path=str(self.iqs), revision=self.commit,
            files={"src/iqs7211e.c": self.digest(self.producer)})))
        (self.owner / "dependencies.json").write_text(json.dumps(self.dependencies))
        self.producer_revision = self.commit
        self.dirty = False
        self.mock = patch.object(IDENTITY.subprocess, "check_output", side_effect=self.git)
        self.mock.start()
        self.addCleanup(self.mock.stop)

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def git(self, command, **kwargs):
        path = Path(command[2])
        if command[3:] == ["rev-parse", "HEAD"]:
            return (self.producer_revision if path == self.iqs else self.dependencies[path.name]["revision"]) + "\n"
        return " M source.c\n" if self.dirty else ""

    def validate(self, producer=None):
        return IDENTITY.validate(self.owner, self.root, producer or self.iqs, self.workspace)

    def test_recorded_current_inputs_are_admitted(self):
        self.assertEqual(self.validate()["compiled_sources"], self.compiled)

    def test_changed_compiled_source_rejected(self):
        self.source.write_text("changed compiled input\n")
        with self.assertRaisesRegex(RuntimeError, "compiled source changed"):
            self.validate()

    def test_changed_producer_bytes_rejected(self):
        self.producer.write_text("changed actual producer\n")
        with self.assertRaisesRegex(RuntimeError, "producer working source changed"):
            self.validate()

    def test_added_producer_source_rejected(self):
        (self.iqs / "src/added.h").write_text("new source input\n")
        with self.assertRaisesRegex(RuntimeError, "producer working source changed"):
            self.validate()

    def test_changed_producer_revision_rejected(self):
        self.producer_revision = "4" * 40
        with self.assertRaisesRegex(RuntimeError, "producer revision changed"):
            self.validate()

    def test_changed_producer_path_rejected(self):
        other = self.iqs.parent / "other-iqs"
        other.mkdir()
        with self.assertRaisesRegex(RuntimeError, "producer path changed"):
            self.validate(other)

    def test_dirty_selected_dependency_rejected(self):
        self.dirty = True
        with self.assertRaisesRegex(RuntimeError, "unchanged selected dependency required"):
            self.validate()

    def test_empty_compiled_measurement_rejected(self):
        (self.owner / "source-after.json").write_text("{}")
        with self.assertRaisesRegex(RuntimeError, "nonempty production source identity"):
            self.validate()

    def test_orchestration_only_change_does_not_require_recompiling_identical_c(self):
        script = self.root / "tests/deep-scan/run.py"
        script.parent.mkdir(parents=True)
        script.write_text("original orchestration\n")
        recorded = dict(self.compiled, **{"tests/deep-scan/run.py": self.digest(script)})
        (self.owner / "source-after.json").write_text(json.dumps(recorded))
        script.write_text("changed orchestration, unchanged compiled C\n")
        self.assertEqual(self.validate()["compiled_sources"], self.compiled)


if __name__ == "__main__":
    unittest.main()
