#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
require_iqs="${ZMK_TEST_REQUIRE_IQS:-0}"
case "$require_iqs" in 0 | 1) ;; *) echo "ZMK_TEST_REQUIRE_IQS must be 0 or 1" >&2; exit 2 ;; esac
if [ "$require_iqs" = 1 ] && [ -z "${ZMK_TEST_IQS_ROOT:-}" ]; then
    echo "IQS producer required: set ZMK_TEST_IQS_ROOT" >&2
    exit 2
fi

docker_args=(run --rm --volume "$repo_root:/src:ro" --env "ZMK_TEST_REQUIRE_IQS=$require_iqs")
if [ -n "${ZMK_TEST_IQS_ROOT:-}" ]; then
    if [ ! -f "$ZMK_TEST_IQS_ROOT/src/iqs7211e.c" ]; then
        echo "IQS producer source missing: $ZMK_TEST_IQS_ROOT" >&2
        exit 2
    fi
    iqs_root="$(CDPATH='' cd -- "$ZMK_TEST_IQS_ROOT" && pwd)"
    docker_args+=(--volume "$iqs_root:/iqs:ro" --env ZMK_TEST_IQS_ROOT=/iqs)
fi

# Persist only explicitly selected fresh results; never reuse prior evidence.
if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    output="$ZMK_TEST_OUTPUT"
    if [[ "$output" != /* ]] || [ -e "$output" ] || [ -L "$output" ]; then
        echo "output must be a fresh absolute path: $output" >&2
        exit 2
    fi
    parent="$(dirname -- "$output")"
    mkdir -p -- "$parent"
    parent="$(CDPATH='' cd -- "$parent" && pwd)"
    docker_args+=(--volume "$parent:/results" --env "ZMK_TEST_OUTPUT=/results/$(basename -- "$output")")
fi

if [ ! -f "${ZMK_TEST_SUPPRESS_ROOT:-}/src/input_processor_suppress.c" ]; then
    echo 'explicit suppression companion required' >&2; exit 2
fi
companion="$(CDPATH='' cd -- "$ZMK_TEST_SUPPRESS_ROOT" && pwd)"
docker_args+=(--volume "$companion:/suppress:ro" --env ZMK_TEST_SUPPRESS_ROOT=/suppress)

docker "${docker_args[@]}" "$image" /bin/bash /src/tests/integration/run.sh "$@"
