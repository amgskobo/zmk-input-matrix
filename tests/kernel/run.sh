#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "pass exactly one resolved west workspace" >&2; exit 2; fi
workspace="$(cd "${1:?pass an already resolved west workspace}" && pwd)"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
output="${ZMK_TEST_OUTPUT:-$workspace/build/matrix-kernel}"
if [ -e "$output" ] || [ -L "$output" ] || [ -e "$output.build.log" ] || [ -L "$output.build.log" ]; then
  echo "output already exists: $output" >&2
  exit 2
fi
cd "$workspace"
mkdir -p "$(dirname "$output")"
west zephyr-export
west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$output" \
  -b native_sim//zmk_test_mock -- -DZMK_CONFIG="$repo/tests/kernel/config" \
  -DZMK_EXTRA_MODULES="$repo;$repo/tests/kernel/module" >"$output.build.log" 2>&1
for run in 1 2 3; do
  timeout 30 "$output/zephyr/zmk.exe" >"$output.run-$run.log" 2>&1
  grep -Fq 'matrix kernel boundary tests: PASS' "$output.run-$run.log"
  cat "$output.run-$run.log"
done
