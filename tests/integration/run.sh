#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail
companion="${ZMK_TEST_SUPPRESS_ROOT:-}"

variant="${1:-upstream}"
case "$variant" in
upstream | dya) ;;
*)
    echo "unknown integration variant '$variant': expected upstream or dya" >&2
    exit 2
    ;;
esac

require_iqs="${ZMK_TEST_REQUIRE_IQS:-0}"
case "$require_iqs" in 0 | 1) ;; *) echo "ZMK_TEST_REQUIRE_IQS must be 0 or 1" >&2; exit 2 ;; esac
if [ "$require_iqs" = 1 ] && [ -z "${ZMK_TEST_IQS_ROOT:-}" ]; then
    echo "IQS producer required: set ZMK_TEST_IQS_ROOT" >&2
    exit 2
fi
if [ -n "${ZMK_TEST_IQS_ROOT:-}" ] && [ ! -f "$ZMK_TEST_IQS_ROOT/src/iqs7211e.c" ]; then
    echo "IQS producer source missing: $ZMK_TEST_IQS_ROOT" >&2
    exit 2
fi

if [ ! -f "$companion/src/input_processor_suppress.c" ]; then
    echo 'explicit suppression companion required' >&2; exit 2
fi

if [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    work_dir="${ZMK_TEST_OUTPUT:?set ZMK_TEST_OUTPUT to a fresh evidence directory}"
    workspace="$(python3 /src/tests/integration/offline.py "$ZMK_TEST_OFFLINE_WORKSPACE" "$variant" "/src/tests/integration/config/$variant/west.yml" "$work_dir")"
    cd "$workspace"
    west zephyr-export
else
if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    work_dir="$ZMK_TEST_OUTPUT"
    if [[ "$work_dir" != /* ]] || [ -e "$work_dir" ] || [ -L "$work_dir" ]; then
        echo "output must be a fresh absolute path: $work_dir" >&2
        exit 2
    fi
    mkdir -p -- "$(dirname -- "$work_dir")"
    mkdir -- "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-matrix-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

cp -R "/src/tests/integration/config/$variant" "$work_dir/config"
cd "$work_dir"

west init -l config
west update --narrow --fetch-opt=--depth=1
west zephyr-export

workspace="$work_dir"
fi

source /src/tests/integration/workspace-env.sh
zmk_test_select_workspace "$workspace"

cmake_args=()
if [ "$variant" = dya ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=/src/tests/integration/custom-settings.conf")
fi

west build -s "$workspace/zmk/app" -d "$work_dir/build" -b xiao_ble/nrf52840/zmk -- \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DZMK_EXTRA_MODULES="/src;/src/tests/integration/firmware;$companion" \
    -DSHIELD=matrix_test \
    "${cmake_args[@]}"

test -f "$work_dir/build/zephyr/zmk.uf2"
grep -q '^CONFIG_ZMK_INPUT_PROCESSOR_MATRIX=y' "$work_dir/build/zephyr/.config"
grep -q '^CONFIG_ZMK_KSCAN_INPUT_MATRIX=y' "$work_dir/build/zephyr/.config"
if [ "$variant" = upstream ]; then
    if grep -q '^CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y' \
        "$work_dir/build/zephyr/.config"; then
        echo "custom settings unexpectedly enabled" >&2
        exit 1
    fi
else
    grep -q '^CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y' \
        "$work_dir/build/zephyr/.config"
    strings "$work_dir/build/zephyr/zmk.elf" >"$work_dir/build/zephyr/strings.txt"
    grep -Fxq amgskobo__matrix "$work_dir/build/zephyr/strings.txt"
    for key in enabled flick_threshold long_press_ms; do
        grep -Fxq "matrix.$key" "$work_dir/build/zephyr/strings.txt" || {
            echo "missing Studio setting: matrix.$key" >&2
            exit 1
        }
    done
    for key in suppress_btn_touch suppress_key; do
        if grep -Fxq "matrix.$key" "$work_dir/build/zephyr/strings.txt"; then
            echo "retired Matrix setting linked: matrix.$key" >&2
            exit 1
        fi
    done
fi

echo "$variant ZMK firmware fixture: PASS"
ZMK_TEST_CUSTOM_SETTINGS=$([ "$variant" = dya ] && echo 1 || echo 0) ZMK_TEST_OUTPUT="$work_dir/kernel" bash /src/tests/kernel/run.sh "$workspace"
producer_args=()
if [ -n "${ZMK_TEST_IQS_ROOT:-}" ]; then
    producer_args+=(--iqs-root "$ZMK_TEST_IQS_ROOT")
fi
ZMK_TEST_CORE="$variant" python3 /src/tests/deep-scan/run.py "$workspace" \
    "$work_dir/ownership" \
    --core-revision "$(git -C "$workspace/zmk" rev-parse HEAD)" "${producer_args[@]}"
