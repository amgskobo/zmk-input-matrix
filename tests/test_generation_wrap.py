# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Generation arithmetic at both atomic word boundaries, actual driver paths."""
import importlib.util
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("matrix_runtime", ROOT / "tests/runtime/run.py")
RUNTIME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNTIME)


class GenerationWrap(unittest.TestCase):
    def test_setter_and_layer_wrap_release_and_replace(self):
        fixture, source, gated, support = RUNTIME.fixtures[1]
        driver, _ = RUNTIME.render_fixture(fixture, source, gated, support)
        probe = (ROOT / "tests/runtime/generation_wrap_harness.c").read_text()
        unit = "#define MATRIX_SOURCE_ALLOCATION_TEST 1\n#define main complete_driver_main\n" + driver + "\n#undef main\n" + probe
        variants = [("sanitized", ["-O1", "-g", "-fsanitize=address,undefined",
                                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"])]
        if os.environ.get("ZMK_TEST_MODE", "quick") == "full":
            variants += [("optimized", ["-O2"]), ("abi32-ubsan", ["-m32", "-O1", "-g",
                                             "-fsanitize=undefined", "-fno-sanitize-recover=all"])]
        retained = os.environ.get("ZMK_TEST_GENERATION_OUTPUT")
        if retained:
            output = Path(retained)
            if not output.is_absolute() or output.exists() or output.is_symlink():
                raise ValueError("generation evidence must be a fresh absolute path")
            output.mkdir(parents=True, exist_ok=False)
            context = contextlib.nullcontext(str(output))
        else:
            context = tempfile.TemporaryDirectory(prefix="matrix-generation-wrap-")
        with context as folder:
            path = Path(folder) / "probe.c"
            path.write_text(unit)
            records = []
            for label, flags in variants:
                for layer in (0, 1):
                    with self.subTest(variant=label, layer=layer):
                        binary = Path(folder) / f"{label}-{layer}"
                        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                   "-DTEST_LISTENER_COUNT=5", f"-DMATRIX_WRAP_LAYER={layer}",
                                   "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                                   "-I" + str(ROOT / "tests/runtime"), str(path), "-o", str(binary)]
                        subprocess.run(command, check=True)
                        identity = hashlib.sha256(binary.read_bytes()).hexdigest()
                        valid = []
                        for repeat in range(1, 4):
                            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
                            if retained:
                                prefix = Path(folder) / f"{label}-{layer}-{repeat}"
                                prefix.with_suffix(".stdout").write_text(result.stdout)
                                prefix.with_suffix(".stderr").write_text(result.stderr)
                                records.append(dict(variant=label, layer=layer, repeat=repeat,
                                    command=command, exit=result.returncode, binary_sha256=identity,
                                    source_sha256=hashlib.sha256((ROOT / "src/input_processor_matrix.c").read_bytes()).hexdigest(),
                                    rendered_sha256=hashlib.sha256(unit.encode()).hexdigest()))
                                (Path(folder) / "results.json").write_text(json.dumps(records, indent=2))
                            print(f"{label} layer={layer} repeat={repeat} exit={result.returncode}: "
                                  + result.stdout + result.stderr, end="")
                            valid.append(result.returncode == 0 and
                                "matrix generation sign/full wrap: hold release and replacement PASS" in result.stdout)
                        self.assertTrue(all(valid), "every same-binary generation-wrap repeat must complete")


if __name__ == "__main__":
    unittest.main()
