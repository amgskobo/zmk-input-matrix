#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Exercise the actual nonempty native mock decoder without modifying the core."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/deep-scan"))
from source_identity import validate

parser = argparse.ArgumentParser()
parser.add_argument("variant", choices=("upstream", "dya"))
parser.add_argument("integration", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
if not args.output.is_absolute() or args.output.exists() or args.output.is_symlink():
    parser.error("output must be a fresh absolute path")
workspace = args.integration / "workspace"
original = json.loads((args.integration / "ownership/build-command.json").read_text())
producer = Path(original["environment"]["MATRIX_TEST_IQS_ROOT"])
before = validate(args.integration / "ownership", ROOT, producer, workspace)
companion = Path(os.environ.get("ZMK_TEST_SUPPRESS_ROOT", "")).resolve()
if not (companion / "src/input_processor_suppress.c").is_file():
    parser.error("explicit ZMK_TEST_SUPPRESS_ROOT required")
jobs = os.environ.get("ZMK_TEST_BUILD_JOBS", "2")
if not jobs.isdigit() or int(jobs) < 1:
    parser.error("ZMK_TEST_BUILD_JOBS must be a positive integer")
args.output.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, **original["environment"], UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
modules = ";".join(map(str, (ROOT, ROOT / "tests/kernel/module", companion,
                             ROOT / "tests/source-limits/mock-module")))
command = ["west", "build", "-p=always", "-o=-j" + jobs, "-s", str(workspace / "zmk/app"),
           "-d", str(args.output / "build"), "-b", "native_sim//zmk_test_mock", "--",
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DZMK_CONFIG=" + str(ROOT / "tests/kernel/config"),
           "-DZMK_EXTRA_MODULES=" + modules, "-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=2",
           "-DCONFIG_ASAN=y", "-DCONFIG_UBSAN=y", "-DCONFIG_ASAN_RECOVER=n",
           "-DEXTRA_DTC_OVERLAY_FILE=" + str(ROOT / "tests/source-limits/active-mock.overlay")]
if args.variant == "dya":
    command += ["-DEXTRA_CONF_FILE=" + str(ROOT / "tests/integration/custom-settings.conf")]
(args.output / "command.json").write_text(json.dumps(dict(command=command, cwd=str(workspace),
    environment={k: env.get(k) for k in ("ZEPHYR_BASE", "Zephyr_DIR", "ZEPHYR_MODULES", "UBSAN_OPTIONS")}), indent=2))
(args.output / "source-before.json").write_text(json.dumps(before, indent=2))
with (args.output / "build.log").open("w") as log:
    result = subprocess.run(command, cwd=workspace, env=env, stdout=log, stderr=subprocess.STDOUT)
(args.output / "build.exit").write_text(str(result.returncode))
if result.returncode:
    raise SystemExit("nonempty mock sanitizer build failed")
binary = args.output / "build/zephyr/zmk.exe"
identity = hashlib.sha256(binary.read_bytes()).hexdigest()
rows = []
for repeat in range(1, 4):
    started = time.time_ns()
    result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=30)
    (args.output / f"run-{repeat}.stdout").write_bytes(result.stdout)
    (args.output / f"run-{repeat}.stderr").write_bytes(result.stderr)
    output = result.stdout + result.stderr
    if result.returncode and b"kscan_mock.c:103" in output and b"left shift of 1 by 31 places cannot be represented" in output:
        status = "BLOCKED: unchanged upstream kscan_mock.c:103 signed shift"
    elif (result.returncode == 0 and b"matrix active nonempty mock decoder: PASS" in output and
          b"matrix kernel boundary tests: PASS" in output):
        status = "PASS"
    else:
        status = "FAIL: unclassified native mock outcome"
    rows.append(dict(repeat=repeat, command=[str(binary)], binary_sha256=identity,
                     started_ns=started, finished_ns=time.time_ns(), exit=result.returncode, status=status))
    (args.output / "results.json").write_text(json.dumps(rows, indent=2))
    print(args.variant, "active nonempty mock", repeat, status, flush=True)
    if status.startswith("FAIL"):
        raise SystemExit(status)
after = validate(args.integration / "ownership", ROOT, producer, workspace)
(args.output / "source-after.json").write_text(json.dumps(after, indent=2))
if before != after:
    raise SystemExit("source drift during nonempty mock boundary")
