# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Admit reuse only for the recorded compiled sources and actual IQS producer."""
import hashlib
import json
from pathlib import Path
import subprocess


def validate(ownership, root, producer, workspace):
    recorded = json.loads((ownership / "source-after.json").read_text())
    compiled = {}
    for name, expected in recorded.items():
        path = root / name
        if path.suffix in (".c", ".h", ".dtsi", ".yaml", ".yml", ".conf", ".overlay") or path.name in ("Kconfig", "CMakeLists.txt"):
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != expected:
                raise RuntimeError("recorded compiled source changed: " + name)
            compiled[name] = actual
    if "src/input_processor_matrix.c" not in compiled:
        raise RuntimeError("nonempty production source identity required")
    iqs = json.loads((ownership / "iqs-after.json").read_text())
    producer = producer.resolve(strict=True)
    if producer != Path(iqs["path"]).resolve(strict=True):
        raise RuntimeError("actual IQS producer path changed")
    revision = subprocess.check_output(["git", "-C", str(producer), "rev-parse", "HEAD"], text=True).strip()
    if revision != iqs["revision"]:
        raise RuntimeError("actual IQS producer revision changed")
    actual_files = {}
    for name in ("src", "Kconfig", "AGENTS.md", "tests/README.md", "tests/maintenance.json"):
        path = producer / name
        for source in sorted(path.rglob("*")) if path.is_dir() else [path]:
            if source.is_file():
                actual_files[source.relative_to(producer).as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
    if actual_files != iqs["files"]:
        raise RuntimeError("actual IQS producer working source changed: files or bytes")
    dependencies = json.loads((ownership / "dependencies.json").read_text())
    for name, values in dependencies.items():
        path = workspace / name
        revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        status = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=all"], text=True)
        if revision != values["revision"] or status:
            raise RuntimeError("unchanged selected dependency required: " + name)
    return dict(compiled_sources=compiled, iqs=iqs, dependencies=dependencies)
