#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Real listener callbacks under default, limited and sparse-ID allocations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("workspace", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    workspace = args.workspace.resolve(strict=True)
    output = args.output
    if not output.is_absolute() or output.exists() or output.is_symlink():
        parser.error("output must be a fresh absolute path")
    companion = Path(os.environ.get("ZMK_TEST_SUPPRESS_ROOT", ""))
    if not (companion / "src/input_processor_suppress.c").is_file():
        parser.error("explicit suppression companion required")
    jobs = os.environ.get("ZMK_TEST_BUILD_JOBS", "2")
    if not jobs.isdigit() or int(jobs) < 1:
        parser.error("ZMK_TEST_BUILD_JOBS must be a positive integer")
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for name, bound, sparse in (("auto-five", 0, False), ("limited-two", 2, False),
                                ("sparse-auto-five", 0, True), ("sparse-rejected-two", 2, True)):
        case = output / name
        case.mkdir()
        build = case / "build"
        command = ["west", "build", "-p=always", "-o=-j" + jobs, "-s", str(workspace / "zmk/app"),
                   "-d", str(build), "-b", "native_sim//zmk_test_mock", "--",
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DZMK_CONFIG=" + str(ROOT / "tests/kernel/config"),
                   "-DZMK_EXTRA_MODULES=" + ";".join(map(str, (ROOT, ROOT / "tests/kernel/module",
                                                               ROOT / "tests/source-limits/module", companion))),
                   "-DCONFIG_MATRIX_KERNEL_TEST=n", "-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=" + str(bound),
                   "-DCONFIG_MATRIX_SOURCE_TEST_LEFT=" + str(3 if sparse else 0),
                   "-DCONFIG_MATRIX_SOURCE_TEST_RIGHT=" + str(4 if sparse else 1),
                   "-DEXTRA_DTC_OVERLAY_FILE=" + str(ROOT / "tests/source-limits" /
                                                        ("sparse.overlay" if sparse else "extra.overlay"))]
        (case / "command.json").write_text(json.dumps(dict(command=command, cwd=str(workspace),
            environment={k: os.environ.get(k) for k in ("ZEPHYR_BASE", "Zephyr_DIR", "ZEPHYR_MODULES")}), indent=2))
        with (case / "build.log").open("w") as log:
            result = subprocess.run(command, cwd=workspace, stdout=log, stderr=subprocess.STDOUT)
        (case / "build.exit").write_text(str(result.returncode))
        if sparse and bound == 2:
            if result.returncode == 0 or "Matrix source limit excludes a referenced listener index" not in (case / "build.log").read_text():
                raise RuntimeError("sparse reference must produce the named allocation rejection")
            results.append(dict(case=name, status="EXPECTED REJECTION", exit=result.returncode))
            print(name + ": named rejection PASS", flush=True)
            continue
        if result.returncode:
            raise RuntimeError(name + ": unexpected build failure")
        binary = build / "zephyr/zmk.exe"
        identity = hashlib.sha256(binary.read_bytes()).hexdigest()
        for repeat in range(1, 4):
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            (case / f"run-{repeat}.log").write_text(result.stdout + result.stderr)
            (case / f"run-{repeat}.exit").write_text(str(result.returncode))
            if result.returncode or "matrix actual listener allocation: PASS" not in result.stdout:
                raise RuntimeError(name + ": nonempty actual-listener completion required")
            results.append(dict(case=name, repeat=repeat, status="PASS", exit=result.returncode,
                                binary_sha256=identity))
            print(name + f": repeat{repeat} PASS", flush=True)
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("Matrix actual-listener allocation: 3 profiles x3 PASS; sparse limit2 named rejection PASS")


if __name__ == "__main__":
    main()
