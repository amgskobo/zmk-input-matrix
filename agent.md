# Input Matrix: implementation contract

Do not commit or create/update PRs without an explicit user request.

Read [AGENTS.md](AGENTS.md) for workflow, checks and evidence rules. Installation
and user configuration belong in [README.md](README.md) and
[README_JA.md](README_JA.md).

## Scope and public interfaces

`zmk,input-processor-matrix` classifies absolute X/Y/touch into Tap and four
Flick directions. `zmk,kscan-input-matrix` exposes the selected cell to ordinary
keymaps. The processor runs only on the split central; peripherals forward raw
input. Preserve binding names, settings keys, subsystem ID and gesture order;
changes require an explicit migration plan.

Bindings are [processor](dts/bindings/zmk,input-processor-matrix.yaml) and
[KSCAN](dts/bindings/zmk,kscan-input-matrix.yaml). Normal KSCAN geometry is
`columns` by `rows * 5`; blocks are Tap, Up, Down, Left, Right in that order.
Diamond mode is processor 1x4/KSCAN 1x4, taps only: Up, Right, Down, Left;
diagonal ties prefer vertical and the exact center maps Down.

The [runtime API](include/zmk-input-matrix/matrix_runtime.h) changes only
`enabled`, `flick_threshold` and `long_press_ms`. Geometry, coordinate ranges,
output device and diamond mode remain compile-time properties. Accept only
this driver's devices, reject zero flick threshold and replace parameters
atomically. Successful updates advance reset generation and resync streams;
zero long-press disables holding. Preserve the public setter reset contract.

## State and output ownership

Node-wide state owns configuration and output selection. All coordinates,
touch/flick/hold state, maximum travel/direction, delayed hold work, contact IDs
and applied generation belong to the listener's `input_device_index` stream.
Multiple local/split listeners may share one node; never share their histories
or replace independence with one node per pad. Size streams from enabled
listeners; invalid indices pass through silently without aliasing stream zero.

Virtual KSCAN owns the union of claims across processors. Emit only the first
press and final release; a tap on an occupied cell adds no edge. Claims outlive
acquisition/release, and failed acquisition must not be released. Preserve
bounded claim/edge capacity and atomic admission reserving the final release.
One FIFO drainer orders concurrent/reentrant output; callbacks run outside locks.
Never mix raw `report_event` with claim ownership APIs for the same cell.

## Event contract

1. Buffer X/Y and clamp to the configured coordinate range.
2. Only off-to-on touch starts a contact; repeated positives do not restart it.
3. The first synchronized touched report with both axes latches origin and
   optionally schedules a hold.
4. Compare squared Euclidean travel against the squared threshold. First
   qualification cancels hold; retain direction at the farthest point.
5. Release emits a held-cell release, latched flick or tap, then clears contact
   buffers so the next touch cannot inherit coordinates.
6. Preserve original KEY type/code/value/sync, including generation discards.
   A reset may neutralize stale coordinates while retaining sync.

Source button disposition and pending HID frames belong to the final filter.
Do not add filter references/APIs/settings to Matrix. `enabled` controls only
gestures. Composition supplies one final instance on every listener route;
Matrix sees only its selected gesture routes and cannot infer bypassed edges.
Runtime edits must not change final masks. Retain complete-pair, ordered-frame,
repeat, overlap, first-click recovery and strict-click composition assertions.

## Hold/reset concurrency

Contact IDs change at every contact/reset. Delayed handlers use contact ID,
`hold_reported` and `hold_release_pending` so any escaping press gets exactly
one release and cannot clear a replacement contact or its deadline.

Keep node reset generation and applied stream generation atomic. Check the
generation on event entry and immediately before cancellation, scheduling or
KSCAN output; a change discards stale work and resynchronizes the stream.
An already claimed hold release still emits once. Re-read generation after
its callback, which can change layers/settings; original KEY edges survive.

Layer/settings resync releases generated holds, cancels delayed work, clears
touch/coordinates/origin/flick, advances contact ID and applies current generation.
Source button history remains with the final owner. Keep contact reduction
separate from KSCAN/work effects and retain release/cancellation assertions.
Callbacks, KSCAN reporting and work operations stay outside stream spinlocks;
parameter copies use their own lock for coherent snapshots.

## Settings and hot paths

With `CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS`, register `amgskobo__matrix`
per processor. Custom Settings exclusively owns persistence; Matrix keeps live
values. Keys use actual node names. Preserve compile-time checks for both RPC
keys and complete `custom_settings/<subsystem>/<key>` storage names.
Boot applies all instances; edits affect only their owner. Serialize the whole
registry read/apply transaction to prevent an older snapshot overwriting edits.
Read settings on settings events, never per coordinate.

Keep coordinate work bounded and allocation-free: no blocking, sleep, settings
I/O/descriptor scans, normal-path logging or KSCAN callbacks under spinlocks.
Keep callback/coordinate words separate from edge polarity without narrowing
public coordinates or reducing capacity. Acquire/release transfers its output
lock to the drainer, which returns unlocked; no nested acquisition. Empty or
already-draining queues only unlock/return. Preserve release reservation/FIFO.

## Build and checks

Attach sources to ZMK's `app` target for the required application headers.
Compile the adapter only when its option is enabled; the base module builds
without DYA Custom Settings. Follow `tests/README.md` for unchanged stock/DYA,
shared-listener, settings/registration, native, producer and negative controls.
For downstream integration changes, build the keyboard's required central,
peripheral and settings-reset targets. Inspect warnings and linked feature keys.

Keep generic examples, SPDX/MIT headers and LF; exclude build products/logs
from publication. Host CPU comparisons require every scenario and real linked
footprint, and do not establish Cortex-M latency, power or physical acceptance.
