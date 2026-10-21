#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Measure actual KSCAN functions with the canonical host stand-ins, not hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/runtime'))
import run as runtime

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--reference', type=Path, help='Explicit unchanged before source for paired comparison')
args = parser.parse_args()
if not args.output.is_absolute() or args.output.exists() or args.output.is_symlink():
    parser.error('output must be a fresh absolute path')
args.output.mkdir(parents=True, exist_ok=False)
fixture = next(row for row in runtime.fixtures if row[0] == 'kscan')
sources = {'current': root / 'src/kscan_input_matrix.c'}
if args.reference is not None:
    sources['reference'] = args.reference.resolve(strict=True)
paths = list(sources.values()) + [root / 'tests/runtime/kscan_harness.c', Path(__file__),
                                root / 'tests/performance/kscan_bench.h', root / 'tests/runtime/run.py']
def identities():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before = identities()
rows = []
measurements = []
with tempfile.TemporaryDirectory(prefix='matrix-host-cpu-') as scratch:
    work = Path(scratch)
    for optimize in ('-Os', '-O2'):
        binaries = {}
        for name, source in sources.items():
            text, _ = runtime.render_fixture(*fixture, source_path=source)
            unit = work / (name + '.c')
            unit.write_text(text)
            binary = work / (name + optimize[1:])
            command = ['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-DMATRIX_PERFORMANCE',
                       '-Wall', '-Wextra', '-Werror', optimize, '-I' + str(root / 'tests/performance'),
                       str(unit), '-o', str(binary)]
            subprocess.run(command, check=True)
            (args.output / (name + optimize[1:] + '.compile.json')).write_text(json.dumps(command, indent=2))
            shutil.copy2(binary, args.output / (name + optimize[1:] + ".exe"))
            binaries[name] = binary
        for repeat in range(7):
            order = list(binaries) if repeat % 2 == 0 else list(reversed(binaries))
            for name in order:
                binary = binaries[name]
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                prefix = f'{name}-{optimize[1:]}-{repeat}'
                (args.output / (prefix + '.stdout')).write_text(result.stdout)
                (args.output / (prefix + '.stderr')).write_text(result.stderr)
                rows.append(dict(source=name, optimize=optimize, repeat=repeat, exit=result.returncode,
                                 binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
                result.check_returncode()
                found = []
                for line in result.stdout.splitlines():
                    if not line.startswith('MEASURE '):
                        continue
                    values = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
                    values.update(source=name, optimize=optimize, repeat=repeat)
                    found.append(values)
                if [row['scenario'] for row in found] != [0, 1, 2]:
                    raise RuntimeError('Missing or unordered scenario results')
                measurements.extend(found)
comparisons = []
if args.reference is not None:
    for optimize in ('-Os', '-O2'):
        for scenario in range(3):
            samples = {name: [row for row in measurements if row['source'] == name and
                             row['optimize'] == optimize and row['scenario'] == scenario]
                       for name in sources}
            for current, reference in zip(samples['current'], samples['reference']):
                for field in ('repeat', 'iterations', 'callbacks', 'checksum'):
                    if current[field] != reference[field]:
                        raise RuntimeError('Reference/current output differs: ' + field)
            medians = {name: statistics.median(row['cpu_ns'] for row in values)
                       for name, values in samples.items()}
            comparisons.append(dict(optimize=optimize, scenario=scenario,
                median_cpu_ns=medians,
                cpu_change_percent=100 * (medians['current'] / medians['reference'] - 1),
                locks={name: sorted(set(row['locks'] for row in values)) for name, values in samples.items()},
                cpu_ns_range={name: [min(row['cpu_ns'] for row in values), max(row['cpu_ns'] for row in values)]
                              for name, values in samples.items()}))
after = identities()
record = dict(source_before=before, source_after=after, runs=rows, measurements=measurements, comparisons=comparisons,
              compiler=subprocess.check_output(['cc', '--version'], text=True),
              platform=platform.platform(),
              limits='Host CPU process time with controlled lock/device stand-ins, 4 claims/8 edges; '
                     'not Cortex-M, real kernel locking, HID latency, radio, power or device acceptance.')
(args.output / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
if before != after:
    raise SystemExit('Source drift during measurement')
print('Host KSCAN paired measurement complete:', args.output)
