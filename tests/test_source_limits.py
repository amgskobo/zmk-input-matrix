# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Exercise the production allocation header with every retained driver case."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("matrix_runtime", ROOT / "tests/runtime/run.py")
RUNTIME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNTIME)


class SourceAllocation(unittest.TestCase):
    def test_complete_driver_allocations(self):
        fixture, source, gated, support = RUNTIME.fixtures[1]
        driver, _ = RUNTIME.render_fixture(fixture, source, gated, support)
        probe = (ROOT / "tests/runtime/source_limits_harness.c").read_text()
        unit = "#define MATRIX_SOURCE_ALLOCATION_TEST 1\n#define main complete_driver_main\n" + driver + "\n#undef main\n" + probe
        variants = [("sanitized", ["-O1", "-g", "-fsanitize=address,undefined",
                                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"])]
        if os.environ.get("ZMK_TEST_MODE", "quick") == "full":
            variants += [("optimized", ["-O2"]), ("abi32", ["-m32", "-O2"])]
        with tempfile.TemporaryDirectory(prefix="matrix-source-limits-") as folder:
            path = Path(folder) / "probe.c"
            path.write_text(unit)
            for label, flags in variants:
                for listeners, bound, expected in ((2, 0, 2), (5, 0, 5), (5, 2, 2),
                                                   (5, 256, 256), (0, 0, 1), (5, 1, 1)):
                    with self.subTest(variant=label, listeners=listeners, bound=bound):
                        binary = Path(folder) / f"{label}-{listeners}-{bound}"
                        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                   f"-DTEST_LISTENER_COUNT={listeners}",
                                   f"-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES={bound}",
                                   f"-DEXPECTED_STREAMS={expected}",
                                   "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                                   "-I" + str(ROOT / "tests/runtime"), str(path), "-o", str(binary)]
                        subprocess.run(command, check=True)
                        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
                        self.assertIn("matrix source allocation: PASS", result.stdout)
                        if expected >= 2:
                            self.assertIn("driver: PASS", result.stdout)
                        print(f"{label} listeners={listeners} bound={bound}: {result.stdout}", end="")

    def test_ignored_positive_bound_has_named_runtime_failure(self):
        fixture, source, gated, support = RUNTIME.fixtures[1]
        driver, _ = RUNTIME.render_fixture(fixture, source, gated, support)
        probe = (ROOT / "tests/runtime/source_limits_harness.c").read_text()
        unit = "#define MATRIX_SOURCE_ALLOCATION_TEST 1\n#define main complete_driver_main\n" + driver + "\n#undef main\n" + probe
        with tempfile.TemporaryDirectory(prefix="matrix-allocation-control-") as folder:
            path, binary = Path(folder) / "probe.c", Path(folder) / "probe"
            path.write_text(unit)
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                       "-DMATRIX_TEST_IGNORE_ALLOCATION=1", "-DTEST_LISTENER_COUNT=5",
                       "-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=2", "-DEXPECTED_STREAMS=2",
                       "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                       "-I" + str(ROOT / "tests/runtime"), str(path), "-o", str(binary)]
            subprocess.run(command, check=True)
            for repeat in range(3):
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Matrix must honor the declared source allocation bound", result.stderr)
                print(f"ignored-allocation control repeat{repeat + 1}: named assertion PASS")


if __name__ == "__main__":
    unittest.main()
