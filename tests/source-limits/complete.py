#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Full existing composition/kernel fixtures with an explicit allocation profile."""
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
args.output.mkdir(parents=True, exist_ok=False)
workspace = args.integration / "workspace"
original = json.loads((args.integration / "ownership/build-command.json").read_text())
env = dict(os.environ, **original["environment"])
env.pop("MATRIX_STRICT_CLICKS", None)
companion = Path(os.environ.get("ZMK_TEST_SUPPRESS_ROOT", "")).resolve()
if not (companion / "src/input_processor_suppress.c").is_file():
    parser.error("explicit ZMK_TEST_SUPPRESS_ROOT required")
jobs = os.environ.get("ZMK_TEST_BUILD_JOBS", "2")
if not jobs.isdigit() or int(jobs) < 1:
    parser.error("ZMK_TEST_BUILD_JOBS must be a positive integer")
producer = Path(original["environment"].get("MATRIX_TEST_IQS_ROOT", ""))
before = validate(args.integration / "ownership", ROOT, producer, workspace)
(args.output / "source-before.json").write_text(json.dumps(before, indent=2))
summary = []

def build(folder, command):
    folder.mkdir()
    (folder / "command.json").write_text(json.dumps(dict(command=command, cwd=str(workspace),
        environment={k: env.get(k) for k in ("ZEPHYR_BASE", "Zephyr_DIR", "ZEPHYR_MODULES", "MATRIX_TEST_IQS_ROOT")}), indent=2))
    started = time.time_ns()
    with (folder / "build.log").open("w") as log:
        result = subprocess.run(command, cwd=workspace, env=env, stdout=log, stderr=subprocess.STDOUT)
    (folder / "build.exit").write_text(str(result.returncode))
    if result.returncode:
        raise RuntimeError("configuration build failed: " + str(folder))
    binary = folder / "build/zephyr/zmk.exe"
    identity = hashlib.sha256(binary.read_bytes()).hexdigest()
    (folder / "binary.sha256").write_text(identity)
    return binary, identity, started

def execute(folder, binary, identity, case, repeat, settings=None, sanitizer=False):
    run_env = dict(env, **(settings or {}))
    if sanitizer:
        run_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    started = time.time_ns()
    result = subprocess.run([str(binary)], env=run_env, capture_output=True, timeout=30)
    prefix = folder / f"{case}-{repeat}"
    prefix.with_suffix(".stdout").write_bytes(result.stdout)
    prefix.with_suffix(".stderr").write_bytes(result.stderr)
    prefix.with_suffix(".exit").write_text(str(result.returncode))
    row = dict(case=case, repeat=repeat, binary_sha256=identity, command=[str(binary)],
               environment=dict(settings or {}, **({"UBSAN_OPTIONS": run_env["UBSAN_OPTIONS"]} if sanitizer else {})),
               started_ns=started, finished_ns=time.time_ns(), exit=result.returncode)
    return result, row

# The original owner build command/selection is reused unchanged apart from its
# fresh output directory and the documented allocation Kconfig value.
folder = args.output / "composition-limited-two"
command = list(original["command"])
command[command.index("-d") + 1] = str(folder / "build")
command = ["-o=-j" + jobs if item.startswith("-o=-j") else item for item in command]
command += ["-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=2"]
binary, identity, _ = build(folder, command)
config = (folder / "build/zephyr/.config").read_text()
if "CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=2\n" not in config:
    raise RuntimeError("limited configuration was not admitted")
selection = json.loads((args.integration / "ownership/selection.json").read_text())
if len(selection["positive_cases"]) != 63:
    raise RuntimeError("full original 46+17 case selection required")
rows = []
for case in selection["positive_cases"]:
    for repeat in range(1, 4):
        result, row = execute(folder, binary, identity, case, repeat, {"MATRIX_REVIEW_CASE": case})
        row["completion"] = ("matrix deep-scan PASS case=" + case).encode() in result.stdout
        rows.append(row)
        (folder / "results.json").write_text(json.dumps(rows, indent=2))
        if result.returncode or not row["completion"]:
            raise RuntimeError("limited composition case failed: " + case)
print(args.variant, "limited2 composition63x3 PASS", flush=True)
summary.append(dict(profile="limited2-composition", status="PASS", cases=63, repeats=3, binary_sha256=identity))

# Reuse this exact limited binary for every existing strict runtime assertion.
for case in ("route-suppressed-release", "route-touch-alias", "route-forwarded-repeat", "route-forwarded-pairs", "iqs-overlap"):
    for repeat in range(1, 4):
        result, row = execute(folder, binary, identity, "strict-" + case, repeat,
                              {"MATRIX_REVIEW_CASE": case, "MATRIX_STRICT_CLICKS": "1"})
        if result.returncode or ("matrix deep-scan PASS case=" + case).encode() not in result.stdout:
            raise RuntimeError("limited strict case failed: " + case)
        rows.append(row)
(folder / "results.json").write_text(json.dumps(rows, indent=2))
summary.append(dict(profile="limited2-strict", status="PASS", cases=5, repeats=3, binary_sha256=identity))

for label, sanitized in (("kernel-limited-two", False), ("kernel-strict-sanitized", True)):
    folder = args.output / label
    modules = ";".join(map(str, (ROOT, ROOT / "tests/kernel/module", companion)))
    command = ["west", "build", "-p=always", "-o=-j" + jobs, "-s", str(workspace / "zmk/app"),
               "-d", str(folder / "build"), "-b", "native_sim//zmk_test_mock", "--",
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DZMK_CONFIG=" + str(ROOT / "tests/kernel/config"),
               "-DZMK_EXTRA_MODULES=" + modules, "-DCONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=2"]
    if args.variant == "dya":
        command += ["-DEXTRA_CONF_FILE=" + str(ROOT / "tests/integration/custom-settings.conf")]
    if sanitized:
        command += ["-DCONFIG_ASAN=y", "-DCONFIG_UBSAN=y", "-DCONFIG_ASAN_RECOVER=n"]
    binary, identity, _ = build(folder, command)
    results = []
    for repeat in range(1, 4):
        result, row = execute(folder, binary, identity, label, repeat, sanitizer=sanitized)
        output = result.stdout + result.stderr
        if sanitized:
            blocked = (result.returncode != 0 and b"kscan_mock.c:103" in output and
                       b"left shift of 1 by 31 places cannot be represented" in output)
            if blocked:
                row["status"] = "BLOCKED: unchanged upstream kscan_mock.c:103 signed shift"
            elif result.returncode == 0 and b"matrix kernel boundary tests: PASS" in output:
                row["status"] = "PASS"
            else:
                raise RuntimeError("unclassified strict native sanitizer outcome")
        else:
            if result.returncode or b"matrix kernel boundary tests: PASS" not in output:
                raise RuntimeError("limited real-kernel fixture did not complete")
            if args.variant == "dya" and b"concurrent registry edits cannot reapply an older Matrix snapshot" not in output:
                raise RuntimeError("DYA real registry race group was missing")
            row["status"] = "PASS"
        results.append(row)
        (folder / "results.json").write_text(json.dumps(results, indent=2))
    summary.append(dict(profile=label, results=results))
    print(args.variant, label, [r["status"] for r in results], flush=True)
after = validate(args.integration / "ownership", ROOT, producer, workspace)
(args.output / "source-after.json").write_text(json.dumps(after, indent=2))
if before != after:
    raise RuntimeError("source drift during complete limited profile")
(args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
