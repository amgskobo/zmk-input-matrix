# Matrix owner checks

Matrix owns contact/gesture reduction, generated virtual-key claims, delayed
holds and settings/layer cancellation. Original source KEY payload/sync remains
transparent. The independent final filter owns source-button disposition and
filtered-frame completion; Matrix settings cannot alter its masks.

```sh
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
# Both unchanged supported cores; run inside the pinned Linux image at /src:
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/stock ZMK_TEST_OUTPUT=/fresh/stock \
ZMK_TEST_SUPPRESS_ROOT=/resolved/suppress ZMK_TEST_IQS_ROOT=/resolved/iqs \
ZMK_TEST_REQUIRE_IQS=1 bash /src/tests/integration/run.sh upstream
# Repeat with dya; additionally provide ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE.
```

| Boundary | Canonical fixture / retained assertions |
| --- | --- |
| Geometry | runtime/geometry_harness.c: actual production config/enum, grid/diamond boundaries, normalized ties, extreme travel and exhaustive small grids |
| Gesture/contact effects | runtime/driver_harness.c: actual types with source-line-marked functions; independent streams, contact/reset generations, pending/reported holds, callback reset/reentry, refusal/recovery and transparent source edges |
| Generated key ownership | runtime/kscan_harness.c: first-owner press/final-owner release, tap-over-hold, invalid/duplicate/orphan/refused claims, atomic capacity/final-release reservation, reentry/FIFO and35-edge wrap |
| Settings adapter | runtime/custom_settings_harness.c: enabled/threshold/hold-delay validation, key bounds, coherent apply and reset |
| Real kernel | kernel/module/main.c: nine actual work/layer/settings hold-cancellation/replacement groups plus DYA registry isolation with ordered KSCAN edges/timestamps; one binary3/core |
| Complete composition | deep-scan/run.py:46 Matrix+17 actual IQS producer cases and five strict cases; real input/listener/keymap/HID, three same-binary runs/core; details in deep-scan/README.md |
| Infrastructure | test_runtime_coverage.py and integration/test_*.py: complete measurement, pinned/clean/safe offline inputs, fresh output, readonly Docker handoff, required IQS/companion failure and child-exit propagation |

Quick runs every host scenario under ASan/UBSan. Full adds optimized/coverage
and retained ABI variants. Real function ranges require executable lines and
complete branches; macro expansions are measured in callers. No behavioral
assertion was dropped. KSCAN reporting/work operations remain outside stream
locks; contact generations/pending releases and output claim/FIFO state are
necessary generated-key behavior, independent of final source suppression.
The unused coordinate constant and logger registration in the silent processor
are removed; active KSCAN/settings logs and their Kconfig level remain.

## Dependencies and evidence

Read root AGENTS.md, agent.md and resolved dependency instructions first.
Offline integration verifies stock5b51501fead672c41b5cfb396f3dafe0894bf4e9 or
DYAe5c9b6915b56801193e359dd9bad4a167ce0d1b8, Zephyr
10ba6d0cb38bc3d258775d27982f707599320085 and required libraries. DYA enables
Custom Settings and checks required/retired setting strings. Its reviewed archive
is commitc6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f; the preparer checks its digest,
without fetching/patching. Existing outputs, source aliases, unsafe/overlapping
project paths and modified dependencies are rejected before writes.

Native standalone: ZMK_TEST_OUTPUT=/fresh/kernel bash tests/kernel/run.sh
/resolved/workspace, with explicit ZMK_TEST_SUPPRESS_ROOT. Integration runs it
and the complete composition scan automatically. CI requires an explicit IQS
checkout, plus reviewed ZMK_SUPPRESS_REPOSITORY/ZMK_SUPPRESS_REVISION variables;
unset required inputs fail rather than reduce case selection. CI keeps selected
logs/config/DTS/binaries and raw3-repeat outputs even on failure.

The optional tests/performance/run.py comparison uses the same canonical host
fixture before/current, Os/O2, seven alternating repetitions and process CPU.
It preserves all scenarios, checksums, lock counts and identities with no speed
threshold. It is not Cortex-M/kernel/latency/power evidence. Production footprint
requires all3 consuming targets with their complete overrides.

## Ownership changes and remaining gaps

Former button-pair/repeat/overlap/pending-frame checks belong to the final owner
and explicit composition, not the Matrix algorithm. No filter link/settings API
exists here. A delayed gesture tap remains silent after lift until the next
new touch; fresh first-click recovery is a current assertion. Historical strict
failures and superseded profile/window/settings contracts remain externally
archived with source/assertion maps; no snapshot was accepted.

Consumer pause/replacement observations asserted replay/old-consumer flushing
that KSCAN does not promise. Their source and named failures3/core were archived
before retirement. They are no longer default expected failures or test modes;
consumer handoff remains unsupported. Default scan requires PASS, with failures
expected only under an explicit defect negative control.

Retired hold-outage/split-diamond simulation retains calculation/state/error
checks above; its physical split/HID/LED/battery timing traces and unresolved
failures remain unvalidated. Source loss/epoch reuse, arbitrary interleavings,
physical sensor/I2C/RDY/split/HID arrivals, latency and power remain separate.
See contract.json/maintenance.json for current commands and limits.

## Scheduled-work and settings regressions

The driver fixture injects replacement before the hold handler's stream lock,
with disabled, changed and unchanged long-press settings, and checks the new
contact's deadline. The kernel fixture pauses a real running handler at the same
unlocked boundary: six retained groups plus three replacement groups, one binary
three times/core. DYA additionally enables the actual settings registry and holds
two processor nodes: a foreign edit changes neither hold; editing the peer releases
only its own claim. The two-node host adapter retains every range/read/reset case
and tests every descriptor, foreign events and initialization. Equal public API
setter calls still reset their own node; notification dispatch is the isolation
boundary. The guarded hold hook is enabled only by the owner kernel fixture.

Concurrent notifications are serialized across the complete settings read/apply
transaction. The DYA kernel fixture wraps the real registry read to pause writer A
after its snapshot, commits writer B on another Zephyr thread, then resumes A.
Live values must finish at the registry's latest values. The same before-fix
binary fails three times; the fixed DYA fixture runs three times. The wrapper
exists only in the owner fixture; production has no settings test hook.

## Selected workspace admission

The owner-local `integration/workspace-env.sh` derives `ZEPHYR_BASE`,
`Zephyr_DIR` and a nonempty `ZEPHYR_MODULES` from the selected workspace.
Discovery failure and empty module lists stop before compilation; ambient cache
modules cannot supply missing dependencies. The canonical host entry runs
`integration/test_workspace_env.py`, including failed-discovery/empty-list
regressions and a symlinked cache with paths containing spaces. The native entry
uses the same selection helper. This validates dependency selection, not hardware.
