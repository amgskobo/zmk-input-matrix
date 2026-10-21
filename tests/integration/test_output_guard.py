# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ENTRY = 'tests/kernel/run.sh'

class FreshOutput(unittest.TestCase):
    def test_existing_output_rejected_before_west_or_any_write(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            output = temp / "existing-evidence"
            output.mkdir()
            sentinel = output / "sentinel.txt"
            sentinel.write_text("preserve prior ordered evidence\n")
            linked = temp / "linked-evidence"
            linked.symlink_to(output, target_is_directory=True)
            dangling = temp / "dangling-evidence"
            dangling.symlink_to(temp / "absent", target_is_directory=True)
            stub = temp / "bin"
            stub.mkdir()
            marker = temp / "west-was-called"
            west = stub / "west"
            west.write_text('#!/bin/sh\necho called >> "$WEST_MARKER"\nexit 7\n')
            west.chmod(0o755)
            env = dict(os.environ, PATH=str(stub) + os.pathsep + os.environ["PATH"],
                       WEST_MARKER=str(marker), ZMK_TEST_OUTPUT=str(output))
            for candidate in (output, linked, dangling):
                with self.subTest(entry=ENTRY, output=str(candidate)):
                    env["ZMK_TEST_OUTPUT"] = str(candidate)
                    command = ["bash", str(root / ENTRY), str(temp)]
                    result = subprocess.run(command, env=env, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            timeout=5, check=False)
                    self.assertEqual(result.returncode, 2, result.stdout)
                    self.assertIn("output already exists", result.stdout)
                    self.assertFalse(marker.exists(), result.stdout)
                    self.assertEqual(sentinel.read_text(), "preserve prior ordered evidence\n")

    def test_existing_and_dangling_build_logs_rejected_before_west(self):
        root = Path(__file__).resolve().parents[2]
        for kind in ('file', 'link', 'dangling'):
            with self.subTest(log=kind), tempfile.TemporaryDirectory() as folder:
                base = Path(folder)
                output = base / 'fresh-evidence'
                build_log = base / 'fresh-evidence.build.log'
                target = base / 'prior-log' if kind == 'link' else base / 'absent-log'
                if kind == 'file':
                    build_log.write_text('preserve raw build evidence\n')
                else:
                    if kind == 'link':
                        target.write_text('preserve raw build evidence\n')
                    build_log.symlink_to(target)
                stub = base / 'bin'
                stub.mkdir()
                marker = base / 'west-was-called'
                west = stub / 'west'
                west.write_text('#!/bin/sh\necho called >> "$WEST_MARKER"\nexit 7\n')
                west.chmod(0o755)
                env = dict(os.environ, PATH=str(stub) + os.pathsep + os.environ['PATH'],
                           WEST_MARKER=str(marker), ZMK_TEST_OUTPUT=str(output))
                result = subprocess.run(['bash', str(root / ENTRY), str(base)], env=env,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('output already exists', result.stderr)
                self.assertFalse(marker.exists())
                self.assertFalse(output.exists())
                if kind == 'dangling':
                    self.assertTrue(build_log.is_symlink())
                    self.assertFalse(target.exists())
                else:
                    self.assertEqual(build_log.read_text(), 'preserve raw build evidence\n')

class NativeFreshOutput(unittest.TestCase):
    def test_native_existing_and_symlink_outputs_are_rejected_before_build(self):
        root=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder)
            output=base/'evidence'; output.mkdir()
            sentinel=output/'sentinel'; sentinel.write_text('keep raw evidence')
            linked=base/'link'; linked.symlink_to(output,target_is_directory=True)
            dangling=base/'dangling'; dangling.symlink_to(base/'absent',target_is_directory=True)
            stub=base/'bin'; stub.mkdir()
            marker=base/'build-was-called'
            for name in ('cc','west'):
                command=stub/name
                command.write_text('#!/bin/sh\necho called >> "$BUILD_MARKER"\nexit 7\n')
                command.chmod(0o755)
            env=dict(os.environ,PATH=str(stub)+os.pathsep+os.environ['PATH'],BUILD_MARKER=str(marker))
            entries=(('tests/deep-scan/run.py',[str(base)],'output already exists'),
                     ('tests/performance/run.py',[],'output must be a fresh absolute path'))
            for entry,arguments,message in entries:
                for candidate in (output,linked,dangling):
                    with self.subTest(entry=entry,output=str(candidate)):
                        result=subprocess.run([sys.executable,str(root/entry),*arguments,str(candidate)],
                                              capture_output=True,text=True,env=env)
                        self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                        self.assertIn(message,result.stderr)
                        self.assertFalse(marker.exists(),result.stdout+result.stderr)
                        self.assertEqual(sentinel.read_text(),'keep raw evidence')

if __name__ == "__main__":
    unittest.main()
