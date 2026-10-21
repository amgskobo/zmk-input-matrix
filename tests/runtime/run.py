# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

"""Exercise all production-driver scenarios in quick or full host mode.

Quick runs every fixture under ASan/UBSan. Full additionally executes the
optimized and measured coverage variants and retains the coverage gate below.

Each fixture names a source file, the functions whose lines and branches it
gates, and any further functions it only needs in order to compile. They are
lifted out of the source, under #line markers that keep their real file and
line numbers, into tests/runtime/<fixture>_harness.c in place of its
/* DRIVER_FUNCTIONS */ marker. The harness supplies stand-ins for Zephyr and
ZMK and drives the functions. Every line and branch inside a gated function
must run, measured from gcov's per-line report rather than its per-file total,
so a support function is compiled in without being held to it.

A name may end in ":2" to lift the second of two definitions -- the other
side of an #if -- and a name starting with "#define " lifts that macro.
"""

import pathlib
import re
import subprocess
import tempfile

import os

mode = os.environ.get("ZMK_TEST_MODE", "quick")
if mode not in ("quick", "full"):
    raise SystemExit("ZMK_TEST_MODE must be quick or full")
variants = (('optimized', ['-O2']), ('sanitized', ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']), ('coverage', ['-O0', '--coverage']))
if mode == "quick":
    variants = tuple(v for v in variants if v[0] == "sanitized")

root = pathlib.Path(__file__).resolve().parents[2]
source_dir = root / "src"
links = ()
# Sources with a coverage gate of their own.
elsewhere = ()
geometry = (
    "clamp_coord_value",
    "calculate_diamond_column",
    "calculate_kscan_coordinates",
    "travel_squared",
    "flick_threshold_squared",
    "flick_enabled",
    "get_gesture_type",
)
fixtures = (
    ("geometry", "input_processor_matrix.c", geometry, ()),
    ("driver", "input_processor_matrix.c", (
        "zip_matrix_params_snapshot",
        "zip_matrix_live_config",
        "zip_matrix_filter_button",
        "zip_matrix_finish_frame",
        "zip_matrix_clear_position",
        "hold_work_handler",
        "stream_for_event",
        "zip_matrix_resync_stream",
        "zip_matrix_reduce_contact",
        "zip_matrix_apply_effects",
        "zip_matrix_process_event",
        "zip_matrix_handle_event",
        "zip_matrix_init",
        "zip_matrix_device_valid",
        "zip_matrix_get_params",
        "zip_matrix_set_params",
        "zip_matrix_release_hold_on_layer_change",
        "#define ZIP_MATRIX_RELEASE_HOLD",
        "zip_matrix_layer_state_listener",
    ), geometry),
    ("kscan", "kscan_input_matrix.c", (
        "zmk_kscan_matrix_report_event",
        "kscan_matrix_enqueue",
        "kscan_matrix_drain",
        "zmk_kscan_matrix_acquire",
        "zmk_kscan_matrix_release",
        "kscan_matrix_configure",
        "kscan_matrix_enable_callback",
        "kscan_matrix_disable_callback",
        "kscan_matrix_init",
        "kscan_matrix_device_valid",
    ), ()),
    ("custom_settings", "input_processor_matrix_custom_settings.c", (
        "matrix_namespace_handler",
        "read_bool",
        "read_int32",
        "matrix_check_unique_keys",
        "#define MATRIX_APPLY",
        "matrix_apply_settings",
        "matrix_settings_event_cb",
    ), ()),
)


def definition(source, name):
    """Return (first line, last line, text) of name's definition."""
    if name.startswith("#define "):
        start = source.index(name)
        end = source.index("\n\n", start) + 1
        first = source.count("\n", 0, start) + 1
        return first, first + source.count("\n", start, end - 1), source[start:end]
    name, _, nth = name.partition(":")
    skip = int(nth or 1) - 1
    for match in re.finditer(r"\b" + name + r"\s*\(", source):
        closing = source.find(")", match.end())
        if not source[closing + 1:].lstrip().startswith("{"):
            continue  # a call or a prototype
        start = source.rfind("\n", 0, match.start()) + 1
        if start == match.start():
            start = source.rfind("\n", 0, start - 1) + 1  # return type on its own line
        head = source[start:match.start()]
        if not head or head[0].isspace() or head.startswith("#"):
            continue  # a call inside a body or a macro
        if skip:
            skip -= 1
            continue
        end = body_end(source, source.index("{", closing))
        first = source.count("\n", 0, start) + 1
        return first, first + source.count("\n", start, end - 1), source[start:end]
    raise RuntimeError(f"missing definition: {name}")


def body_end(source, opening):
    """Index just past the brace that closes the one at opening.

    Braces inside comments and string or character literals do not count.
    """
    depth = 0
    i = opening
    while True:
        if source.startswith("/*", i):
            i = source.index("*/", i) + 2
            continue
        if source.startswith("//", i):
            i = source.index("\n", i)
            continue
        char = source[i]
        if char in "\"'":
            i += 1
            while source[i] != char:
                i += 2 if source[i] == "\\" else 1
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


def misses(gcov_text, ranges):
    """Count lines and branches inside ranges; list what never ran."""
    lines = executed = branches = taken = 0
    observed = [0 for _range in ranges]
    missed = []
    current = None
    for row in gcov_text.splitlines():
        if row.startswith("branch"):
            if current is None:
                continue
            branches += 1
            if re.search(r"taken [1-9]", row):
                taken += 1
            else:
                missed.append(f"  {current[1]}: {current[2].strip()}\n    {row}")
            continue
        found = re.match(r"\s*([^:]+):\s*(\d+):(.*)", row)
        if not found:
            continue
        count, number, text = found.group(1).strip(), int(found.group(2)), found.group(3)
        inside = any(first <= number <= last for first, last in ranges)
        current = (count, number, text) if inside else None
        if not inside or count == "-":
            continue
        lines += 1
        for index, (first, last) in enumerate(ranges):
            if first <= number <= last:
                observed[index] += 1
        if count.startswith(("#####", "=====")):
            missed.append(f"  {number}: {text.strip()}  (never ran)")
        else:
            executed += 1
    if not ranges or not lines:
        raise RuntimeError("coverage report has no executable lines in gated functions")
    if any(count == 0 for count in observed):
        raise RuntimeError("coverage report has no executable lines for a gated function range")
    return lines, executed, branches, taken, missed


def ungated_functions():
    """Functions defined in the sources that no fixture gates.

    A file named in `elsewhere` has a gate of its own and is skipped, so a
    function added to any other source without a test fails this run.
    """
    gated = {}
    for _fixture, file_name, names, _support in fixtures:
        gated.setdefault(file_name, set()).update(name.split(":")[0] for name in names)
    definition_pattern = re.compile(
        r"^(?:static\s+|__weak\s+)?(?:inline\s+)?(?:const\s+)?[a-z_0-9]+(?:\s+[a-z_0-9]+)?"
        r"[\s*]+([a-z_0-9]+)\([^;]*?\)\s*\{", re.M)
    missing = []
    for path in sorted(source_dir.glob("*.c")):
        if path.name in elsewhere:
            continue
        defined = set(definition_pattern.findall(path.read_text(encoding="utf-8")))
        missing += [f"{path.name}: {name}" for name in sorted(defined - gated.get(path.name, set()))]
    return missing


def render_fixture(fixture, file_name, gated, support, source_path=None):
    source_path = source_path or source_dir / file_name
    source = source_path.read_text(encoding="utf-8")
    functions = []
    ranges = []
    for name in (*support, *gated):
        first, last, text = definition(source, name)
        # Macro expansions are measured in their calling functions.
        if name in gated and not name.startswith("#"):
            ranges.append((first, last))
        elif name not in gated and not name.startswith("#"):
            # A harness need not call every function it only compiles.
            text = "__attribute__((unused)) " + text
        functions.append(f'#line {first} "{source_path}"\n{text}')

    harness_path = root / "tests/runtime" / f"{fixture}_harness.c"
    harness = harness_path.read_text(encoding="utf-8")
    marker = "/* DRIVER_FUNCTIONS */"
    resume = harness.count("\n", 0, harness.index(marker)) + 2
    if "/* DRIVER_TYPES */" in harness:
        types = []
        names = ("kscan_matrix_edge", "kscan_matrix_data", "kscan_matrix_config") if fixture == "kscan" else (
            "zip_matrix_config", "zip_matrix_contact", "zip_matrix_buttons", "zip_matrix_effects",
            "zip_matrix_stream", "zip_matrix_data")
        if fixture == "driver":
            for name in ("gesture_type", "zip_matrix_key_action"):
                match = re.search(r"^enum " + name + r" \{.*?^\};", source, re.M | re.S)
                if match is None:
                    raise RuntimeError("missing enum: " + name)
                types.append(match.group())
            types.append("struct zip_matrix_data;")
        for name in names:
            match = re.search(r"^struct " + name + r" \{.*?^\};", source, re.M | re.S)
            if match is None:
                raise RuntimeError("missing structure: " + name)
            types.append(match.group())
        harness = harness.replace("/* DRIVER_TYPES */", "\n".join(types))
    return harness.replace(marker, "\n".join(functions) +
                           f'\n#line {resume} "{harness_path}"'), ranges


def main():
    missing_functions = ungated_functions()
    if missing_functions:
        raise RuntimeError("functions no fixture gates:\n  " + "\n  ".join(missing_functions))

    for fixture, file_name, gated, support in fixtures:
        unit_text, ranges = render_fixture(fixture, file_name, gated, support)
        with tempfile.TemporaryDirectory(prefix=f"{fixture}-runtime-") as folder:
            work = pathlib.Path(folder)
            unit = work / "test.c"
            unit.write_text(unit_text, encoding="utf-8")
            for variant, flags in variants:
                binary = work / variant
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                "-I" + str(root / "include"), "-I" + str(source_dir),
                                "-I" + str(root / "tests/runtime"),
                                str(unit), *map(str, links), "-o", str(binary)],
                               check=True)
                subprocess.run([str(binary)], check=True)
            if fixture == "kscan":
                binary = work / "wide-queue-sanitized"
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                "-DCONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE=35",
                                str(unit), "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True)
                print("kscan: non-power-of-two 35-edge FIFO wrap and edge polarity PASS")
            if mode == "quick":
                print(f"{fixture}: PASS (all scenarios, ASan/UBSan)")
                continue
            subprocess.run(["gcov", "-b", "-c", "-o", str(work / "coverage-test.gcno"), str(unit)],
                           cwd=work, check=True, capture_output=True, text=True)
            report = (work / f"{file_name}.gcov").read_text(encoding="utf-8")
            lines, executed, branches, taken, missed = misses(report, ranges)
            count = sum(not name.startswith("#") for name in gated)
            print(f"{fixture}: {count} functions of {file_name}: "
                  f"lines {executed}/{lines}, branches {taken}/{branches}")
            if missed:
                print("\n".join(missed))
                raise RuntimeError(f"{fixture} fell below 100% coverage")


if __name__ == "__main__":
    main()
