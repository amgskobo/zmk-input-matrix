"""Compile the driver's actual coordinate and gesture functions on the host."""

import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "src/input_processor_matrix.c").read_text(encoding="utf-8")
names = (
    "clamp_coord_value",
    "calculate_diamond_column",
    "calculate_kscan_coordinates",
    "travel_squared",
    "flick_threshold_squared",
    "flick_enabled",
    "get_gesture_type",
)
functions = []
for name in names:
    match = re.search(r"static\s+[^;{}]+?\b" + name + r"\([^;{}]+?\)\s*\{", source)
    if match is None:
        raise RuntimeError(f"driver function missing: {name}")
    end = source.index("\n}", match.end()) + 2
    functions.append(source[match.start():end])

harness = (root / "tests/geometry/harness.c").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="matrix-geometry-") as folder:
    unit = pathlib.Path(folder) / "test.c"
    unit.write_text(harness.replace("/* DRIVER_FUNCTIONS */", "\n".join(functions)), encoding="utf-8")
    for variant, flags in (
        ("optimized", ["-O2"]),
        ("sanitized", ["-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]),
        ("coverage", ["-O0", "--coverage"]),
    ):
        binary = pathlib.Path(folder) / variant
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        *flags, str(unit), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    subprocess.run(["gcov", "-b", "-c", "-o", str(pathlib.Path(folder) / "coverage-test.gcno"),
                    str(unit)],
                   cwd=folder, check=True)
