#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Reuse a completed owner binary for strict cases; build genuine defect controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from source_identity import validate

ROOT = Path(__file__).resolve().parents[2]

parser = argparse.ArgumentParser()
parser.add_argument("variant", choices=("upstream", "dya"))
parser.add_argument("workspace", type=Path)
parser.add_argument("ownership", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
companion = Path(os.environ.get("ZMK_TEST_SUPPRESS_ROOT", "")).resolve()
producer = Path(os.environ.get("ZMK_TEST_IQS_ROOT", "")).resolve()
if not (companion / "src/input_processor_suppress.c").is_file() or not (producer / "src/iqs7211e.c").is_file():
    parser.error("explicit ZMK_TEST_SUPPRESS_ROOT and ZMK_TEST_IQS_ROOT required")
if not args.output.is_absolute() or args.output.exists() or args.output.is_symlink():
    parser.error("output must be a fresh absolute path")
args.output.mkdir(parents=True, exist_ok=False)
before = validate(args.ownership, ROOT, producer, args.workspace)
(args.output / "source-before.json").write_text(json.dumps(before, indent=2))
binary = args.ownership / "zmk.exe"
sha = hashlib.sha256(binary.read_bytes()).hexdigest()
if sha != (args.ownership / "binary.sha256").read_text():
    raise SystemExit("existing composition binary identity mismatch")
rows = []
strict = ("route-suppressed-release", "route-touch-alias", "route-forwarded-repeat",
          "route-forwarded-pairs", "iqs-overlap")
for case in strict:
    for repeat in range(1, 4):
        env = dict(os.environ, MATRIX_REVIEW_CASE=case, MATRIX_STRICT_CLICKS="1")
        started = time.time_ns()
        result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=15)
        prefix = args.output / f"strict-{case}-{repeat}"
        prefix.with_suffix(".stdout").write_bytes(result.stdout)
        prefix.with_suffix(".stderr").write_bytes(result.stderr)
        row = dict(case=case, repeat=repeat, command=[str(binary)], binary_sha256=sha,
                   environment={"MATRIX_REVIEW_CASE": case, "MATRIX_STRICT_CLICKS": "1"},
                   started_ns=started, finished_ns=time.time_ns(), exit=result.returncode,
                   completion=("matrix deep-scan PASS case=" + case).encode() in result.stdout)
        rows.append(row)
        (args.output / "strict-results.json").write_text(json.dumps(rows, indent=2))
        if result.returncode or not row["completion"]:
            raise SystemExit("strict owner case failed: " + case)
        print("strict", case, repeat, "PASS", flush=True)
env = dict(os.environ, ZMK_TEST_CORE=args.variant, ZMK_TEST_SUPPRESS_ROOT=str(companion))
core = subprocess.check_output(["git", "-C", str(args.workspace / "zmk"), "rev-parse", "HEAD"], text=True).strip()
for control in ("inactive-matrix", "iqs-inline-first", "iqs-drop-release"):
    command = ["python3", str(ROOT / "tests/deep-scan/run.py"), str(args.workspace),
               str(args.output / control), "--core-revision", core, "--negative-control", control]
    if control != "inactive-matrix":
        command += ["--iqs-root", str(producer), "--iqs-only"]
    (args.output / (control + ".command.json")).write_text(json.dumps(command, indent=2))
    with (args.output / (control + ".log")).open("w") as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    (args.output / (control + ".exit")).write_text(str(result.returncode))
    print(control, "exit", result.returncode, flush=True)
    if result.returncode:
        raise SystemExit("owner defect-control entry failed: " + control)
after = validate(args.ownership, ROOT, producer, args.workspace)
(args.output / "source-after.json").write_text(json.dumps(after, indent=2))
if before != after:
    raise SystemExit("source drift during owner post checks")
print("Existing owner strict5x3 and inactive1x3/inline2x3/drop1x3 named controls PASS")
