# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import tempfile
import shutil
import os
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("offline", Path(__file__).with_name("offline.py"))
offline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(offline)
if os.environ.get("ZMK_TEST_OFFLINE_NEGATIVE_CONTROL") == "skip-identity":
    offline.identity = lambda path, expected: expected
if os.environ.get("ZMK_TEST_OFFLINE_NEGATIVE_CONTROL") == "skip-archive-digest":
    class _UncheckedDigest:
        def hexdigest(self):
            return offline.CUSTOM_ARCHIVE
    offline.hashlib.sha256 = lambda data: _UncheckedDigest()

class OfflineAdmission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        (self.source / ".west").mkdir(parents=True)
        (self.source / ".west/config").write_text("[manifest]\npath=config\n")
        self.manifest = self.root / "west.yml"
        self.manifest.write_text("manifest: {}\n")
        self.output = self.root / "output"
        self.pins = dict(offline.PINS, zmk=offline.CORES["upstream"], **{"zmk-studio-messages": offline.STUDIO["upstream"]})
        for name in self.pins:
            (self.source / name).mkdir()
        self.rows = "\n".join(f"{name}|{name}|{self.source / name}" for name in self.pins)
        self.dirty = False
        self.bad_pin = False
        self.mock = patch.object(offline, "run", side_effect=self.run_command)
        self.mock.start()
        self.addCleanup(self.mock.stop)
        core_export = patch.object(offline, "export_core", side_effect=shutil.copytree)
        core_export.start()
        self.addCleanup(core_export.stop)

    def run_command(self, *args, cwd=None):
        if args[0] == "west":
            return self.rows
        name = Path(args[2]).name
        if args[3] == "status":
            return " M source.c" if self.dirty else ""
        return "0" * 40 if self.bad_pin else self.pins[name]

    def prepare(self, variant="upstream"):
        return offline.prepare(self.source, variant, self.manifest, self.output)

    def test_valid_sources_are_symlinked_and_manifest_preserved(self):
        workspace = self.prepare()
        self.assertEqual((workspace / "config/west.yml").read_text(), self.manifest.read_text())
        self.assertTrue((workspace / "zephyr").is_symlink())
        self.assertTrue((workspace / "zmk").is_dir())
        self.assertFalse((workspace / "zmk").is_symlink())
        self.assertTrue((self.output / "dependencies.json").is_file())

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.prepare()

    def test_wrong_core_variant_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "wrong dependency revision"):
            self.prepare("dya")
        self.assertFalse(self.output.exists())

    def test_wrong_dependency_pin_is_rejected(self):
        self.bad_pin = True
        with self.assertRaisesRegex(ValueError, "wrong dependency revision"):
            self.prepare()

    def test_dirty_sources_are_rejected(self):
        self.dirty = True
        with self.assertRaisesRegex(ValueError, "local changes"):
            self.prepare()

    def test_missing_dependency_is_rejected(self):
        self.rows = "\n".join(self.rows.splitlines()[1:])
        with self.assertRaisesRegex(ValueError, "missing required dependency"):
            self.prepare()

    def test_path_escape_is_rejected(self):
        self.rows += "\nunsafe|../escape|/outside"
        with self.assertRaisesRegex(ValueError, "unsafe project path"):
            self.prepare()

    def test_custom_settings_requires_exact_archive(self):
        self.manifest.write_text("zmk-feature-custom-settings\n")
        with patch.dict(offline.os.environ, {"ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE": str(self.root / "missing")}):
            with self.assertRaisesRegex(ValueError, "pinned custom-settings"):
                self.prepare()
        self.assertFalse(self.output.exists())

    def test_existing_custom_settings_archive_with_wrong_digest_is_rejected(self):
        self.manifest.write_text("zmk-feature-custom-settings\n")
        archive = self.root / "wrong-archive.tar"
        archive.write_bytes(b"controlled invalid archive bytes")
        with patch.dict(offline.os.environ, {"ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE": str(archive)}):
            with self.assertRaisesRegex(ValueError, "pinned custom-settings"):
                self.prepare()
        self.assertFalse(self.output.exists())

if __name__ == "__main__":
    unittest.main()
