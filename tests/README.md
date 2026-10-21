# Module-owned tests

Run from this checkout. Docker + Bash is the host entry point; no keyboard
checkout, shared test dispatcher or simulator repository is required.

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

On PowerShell, set `$env:ZMK_TEST_MODE="full"` for the second command
and invoke Git Bash explicitly. Remove the variable for the default quick run.

Quick executes **all behavioral fixtures and scenarios** once with
AddressSanitizer/UndefinedBehaviorSanitizer. Full repeats those same assertions
under the retained optimization, coverage and (where already present) 32-bit
variants. CI uses quick for push/PR and full for scheduled/manual runs.
Unknown mode values fail; there is no silent skipped success.

## Retained behavioral contracts

| Fixture | Assertions |
| --- | --- |
| [runtime/geometry_harness.c](runtime/geometry_harness.c) | Grid/diamond geometry and boundary classification |
| [runtime/driver_harness.c](runtime/driver_harness.c) | Gesture identity, contact generations, hold/cancel and balanced releases, activation-boundary click/frame state and release-callback reset |
| [runtime/kscan_harness.c](runtime/kscan_harness.c) | Virtual KSCAN output contract |
| [runtime/custom_settings_harness.c](runtime/custom_settings_harness.c) | Settings validation and reset |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual full runtime entry rejects empty, unrelated and partially measured reports |

Each fixture keeps its existing normal, boundary/invalid, independent-state,
reset/cancellation and failure/recovery scenarios where applicable. Checks
assert ordered outputs and explicit time inputs. Coverage is a full-mode gate,
not a reason to replace assertions or a hardware completeness claim.

The driver fixture also pins releases across the final generation check:
lift-frame and new-contact stale holds each produce exactly one release for the
escaped press; forwarded button releases observed here survive on both streams, while the
release of a suppressed press stays consumed. Fresh presses and motion remain
discarded across a reset. The release-callback fixture changes settings during
output dispatch and verifies that remaining old-generation effects are rejected. These are injected state transitions, separate from
the actual-workqueue and physical-delivery checks below.

The canonical host/CI entry requires executable gcov lines for every gated
real function; macros are measured in their callers. Recording tools exercise
the actual full entry, while normal full runs retain measured compiled coverage
and all behavioral assertions. Offline staging rejects outputs inside resolved
sources/dependencies and parent-symlink aliases, overlapping project paths and
west/config metadata collisions before writing. External dependencies and
sibling outputs remain supported. The parseable wrong-archive control must fail
its missing-digest-rejection assertion when that check is bypassed.

## Separate integration and physical contracts

Keep the existing owner `run-integration-docker.sh` commands and accepted
upstream/DYA variants for changed APIs, DTS/Kconfig/registration or actual
kernel boundaries. Native fixtures and negative guards are retained. Do not
run all firmware variants for a pure arithmetic edit. Host extracted functions
and controlled work queues do not prove real scheduling or transport delivery.


`tests/kernel/` uses the whole production driver, actual delayed work/spinlocks,
layer events and the real KSCAN proxy, with a recording callback at its output.
Six scenarios cover independent holds, overdue queued cancellation, settings
disable and layer changes. The integration command runs the same binary three
times. For a previously resolved workspace, run
`bash tests/kernel/run.sh /absolute/west/workspace`; `ZMK_TEST_OUTPUT` chooses
the evidence directory. Resolve/read its dependencies before running. This
does not fetch versions and does not prove downstream KSCAN queue, HID or split
delivery.

## Retired legacy simulation

The active `tests/bsim` directory was removed on 2026-10-05. Before removal,
every source and ordered snapshot was SHA256-verified in an external archive.
Its retirement manifest retains the raw traces and assertion mapping. Archive
locations belong to the consuming keyboard integration repository and are not
a prerequisite for these module-owned checks.
No failed snapshot was accepted as passing.

| Retired case | Behavioral replacement | Transport boundary |
| --- | --- | --- |
| hold-outage | The owner fixtures above retain state/arithmetic/error assertions | Ordered physical split/HID/LED/battery delivery and timing: pending actual-device evidence |
| split-diamond | The owner fixtures above retain state/arithmetic/error assertions | Ordered physical split/HID/LED/battery delivery and timing: pending actual-device evidence |

For the affected case, exercise the same contact/layer/settings/power or
congestion/reconnect transition on the production board. Capture each report
value/order/timestamp and verify balanced releases and specified deadlines.
Do not mark the archived exact radio trace as covered by a host stub.

## Current test ownership and compatibility

Run the relevant public interface/build checks on both unchanged stock ZMK
and DYA when this owner supports both. See the selected core in the manifest.

Canonical entries (run from the repository root):

```bash
bash tests/run-docker.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

## CI producer and retained evidence

Both firmware CI jobs check out the merged IQS commit
`6114ec0b6ac859380125afd60a9c042a0cfa2406` separately from the production
manifest and use a digest-pinned build image. They set `ZMK_TEST_IQS_ROOT` and
`ZMK_TEST_REQUIRE_IQS=1` and invoke the standard Docker entry. The checkout is mounted read-only at `/iqs`.
Missing IQS input fails before dependency preparation or builds; it cannot
silently reduce CI to the 45 Matrix cases. Both cores run all 62 cases three
times, using the unchanged producer source from that commit. Local IQS edits
are a separate source identity and do not establish the committed-source result.

For the same selection locally:

```bash
ZMK_TEST_REQUIRE_IQS=1 ZMK_TEST_IQS_ROOT=/absolute/iqs-checkout \
  ZMK_TEST_OUTPUT=/absolute/fresh-results \
  bash tests/run-integration-docker.sh upstream
# Repeat with dya and another fresh result path.
```

`ZMK_TEST_OUTPUT` is an optional fresh absolute host path on the Docker entry.
The online integration retains that directory on success or failure. CI uploads
the build log, native/kernel raw ordered runs and identities, config, DTS and
UF2 even after failures; dependency clones are not uploaded. Without an explicit
output, the online manual entry keeps its temporary cleanup behavior. Without
required mode, an omitted IQS checkout remains an explicit IQS NOT RUN.
The handoff/admission regressions are in
`tests/integration/test_docker_entry.py`; they run under the existing unittest
discovery, including CI. They check readonly mounting, required-input failure,
fresh-output protection and child-failure propagation, not hardware behavior.

## Explicit offline integration

The existing `tests/integration/run.sh` also accepts an already resolved cache
through `ZMK_TEST_OFFLINE_WORKSPACE`, with a fresh `ZMK_TEST_OUTPUT` directory.
Run this entry inside the same build image with the module mounted at `/src`.
The offline path makes an isolated west workspace using the original fixture
manifest, an unchanged local Git clone of ZMK and symlinks to cached dependencies; it never fetches or patches
the core. Exact supported core, Zephyr and required library revisions and clean
source state are checked before a build; existing outputs are rejected.
Select `upstream` or `dya` as the first argument.

DYA fixtures that enable custom settings require
`ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE`: the SHA256-checked git archive of the pinned
`c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f` test dependency, without local edits.
The accepted archive digest is recorded in `tests/integration/offline.py`.
All existing fixture assertions and cases run unchanged after preparation.
Test offline admission and rejection with
`python3 -m unittest discover -s tests/integration -p test_offline.py -v`.
Core updates require an explicit pin review rather than an implicit fetch.

The preparation uses Linux symlinks and runs inside the documented build image.
`ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-identity` in the admission test command
intentionally bypasses dependency identity validation; its rejection tests must fail.

Standalone native/kernel entries reject existing output directories and symlinks
before invoking west or touching prior logs. The kernel entry also rejects existing
build-log files and links, including dangling links, before dependency tools run. The host entry also runs
`test_output_guard.py`, which preserves a sentinel and checks zero west calls
for existing, linked and dangling output paths.

For example, run the stock fixture inside the build image:

```bash
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/upstream \
ZMK_TEST_OUTPUT=/tmp/matrix-upstream-v1 \
bash /src/tests/integration/run.sh upstream
```

Select `dya` with the resolved DYA workspace for its variant; provide the pinned
custom-settings archive when that fixture enables custom settings.

## Shared-cell ownership regression and remaining observations

[The focused native scan](deep-scan/README.md) retains the former overlap
counterexample as a passing ownership regression, with tap/hold, separate
processor, callback reentry and concurrent-producer cases. The superseded
`review-overlap` entry was retired after preserving its source and failure
evidence externally and mapping every assertion to the retained cases.

The scan has 45 Matrix cases and adds17 actual IQS producer cases with an
explicit `--iqs-root /resolved/iqs` (or `ZMK_TEST_IQS_ROOT` on the combined
integration entry). The six superseded synthetic click cases and empty legacy
selector are removed, with source/failure evidence and every assertion mapped
before removal. All62 required cases check release safety for complete
source pairs and retain normal/invalid/reset/cancellation/state/frame coverage.
Cross-route early release is permitted under this contract, logged, and checked
by a separate `--strict-clicks` command whose five failures remain FAIL rather
than accepted negative controls. ON/ON/OFF outside Matrix is retained there;
the required route case uses complete ON/OFF pairs and verifies recovery.
No explicit IQS checkout means IQS NOT RUN. See its README for commands and
the sensor/split/physical limits. The two consumer-lifecycle observations remain
separate unsupported-API observations.

## Focused efficiency comparison

Inside the same Linux build image, run:

```sh
python3 tests/performance/run.py /fresh/cpu-results --reference /reviewed/before/kscan_input_matrix.c
```

The explicit before source is compiled unchanged with the same canonical host
fixture and compiler flags as the current implementation. It reuses the
conformance assertions, then measures single-cell, shared-cell and four-cell
acquire/release cycles. Each of `-Os` and `-O2` uses one binary per source, seven
alternating-order repetitions and process CPU time. Raw outputs, ordered
scenario results, lock counts, output checksums, commands, binaries and source
identities are retained. CPU comparisons have no pass threshold; output
disagreement, process failure or source drift fails the run. The default omits
the reference and records only the current implementation.

These are host measurements with controlled lock/device stand-ins and a
four-claim/eight-edge fixture, not Cortex-M execution, real kernel locking,
input latency or power. The normal host entry additionally checks exact FIFO
values/order through a non-power-of-two 35-edge capacity, including wraparound.
Production RAM/Flash require the consuming keyboard's normal three builds.
