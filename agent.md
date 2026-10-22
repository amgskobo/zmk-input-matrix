# Maintainer Guide: ZMK Input Matrix

This file is the implementation contract for maintainers and coding agents.
User-facing installation and configuration belong in `README.md` and
`README_JA.md`; keep this document focused on invariants, concurrency, test
coverage, and release checks.

## Scope

The module contains two device types:

- `zmk,input-processor-matrix` consumes absolute X/Y and touch events and
  classifies a contact as Tap, Flick Up, Flick Down, Flick Left, or Flick Right.
- `zmk,kscan-input-matrix` is a virtual KSCAN output. The processor reports the
  selected cell through it so an ordinary ZMK keymap can bind the gesture.

The input processor is central-only on split keyboards. A peripheral may send
input to the central, but it must not instantiate an independent processor.

## Public interfaces

### Devicetree

Processor properties are defined by
`dts/bindings/zmk,input-processor-matrix.yaml`:

- `rows`, `columns`: grid dimensions.
- `x`, `y`: maximum absolute coordinates.
- `flick-threshold`: minimum Euclidean travel for a flick.
- `long-press-ms`: delay before a Tap cell is held; zero disables holding.
- `kscan`: virtual KSCAN output device.
- `diamond-tap`: taps-only 1x4 diagonal partition.

The KSCAN properties are defined by
`dts/bindings/zmk,kscan-input-matrix.yaml`. Its dimensions must match the
processor output:

- normal mode: `columns` must match and KSCAN rows must be `rows * 5`;
- diamond mode: `columns = 4`, processor `rows = 1`, and KSCAN `rows = 1`.

Do not rename existing properties or change gesture row order. Both are public
interfaces used by downstream devicetrees and keymaps.

### Runtime API

`include/zmk-input-matrix/matrix_runtime.h` exposes get/set operations for:

- `enabled`;
- `flick_threshold`;
- `long_press_ms`.

Structural properties (`rows`, `columns`, `x`, `y`, `kscan`, and
`diamond-tap`) remain compile-time configuration. Changing them at runtime
would invalidate the KSCAN geometry or keymap.

The setter accepts only devices instantiated by this driver and rejects a zero
flick threshold. A successful update replaces the parameter set atomically
from a reader's point of view, increments the reset generation, and resyncs
every listener stream.

### DYA custom settings

When `CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y`, the module registers
`amgskobo__matrix` and publishes one setting set per processor instance.
Persistence is owned exclusively by `zmk-feature-custom-settings`; the matrix
driver holds only live values.

Setting keys are `<devicetree-node-name>.<field>`. Both the RPC key and full
`custom_settings/<subsystem>/<key>` storage name are checked at compile time.
Never remove these checks: otherwise an RPC write may appear successful but
fail later when Zephyr tries to persist an overlong name.

## Gesture layout

Normal mode stacks five equally sized blocks in this fixed order:

| Block | Gesture | Row offset |
| ---: | --- | ---: |
| 0 | Tap | `rows * 0` |
| 1 | Flick Up | `rows * 1` |
| 2 | Flick Down | `rows * 2` |
| 3 | Flick Left | `rows * 3` |
| 4 | Flick Right | `rows * 4` |

Diamond mode reports taps only. Columns are Up, Right, Down, Left. A point on
a diagonal prefers the vertical axis; the exact center maps to Down.

## State ownership

Configuration and the output KSCAN device belong to the processor node.
The virtual output owns the union of active source claims across all processor
nodes. Only its first owner emits press and only its final owner emits release.
A tap overlapping an occupied cell emits no extra edge. The source claim must
outlive acquisition and release; a rejected acquisition must never be released.
Claims and pending edges have explicit Kconfig bounds. Admission is atomic and
reserves the eventual final release; capacity failure cannot strand an accepted
owner. A single drainer preserves edge order across concurrent/reentrant calls,
with callbacks outside locks. Raw report_event remains an unaggregated legacy
API; never mix raw and ownership APIs on one cell. All
contact state belongs to an input stream selected by
`zmk_input_processor_state.input_device_index`:

- current and start coordinates;
- touch, flick, and hold state;
- maximum flick travel and direction;
- delayed hold work;
- contact identifiers;
- applied reset generation.

This split is mandatory. Multiple listeners, including a local pad and a
split-proxied pad, may share one processor node. They must never overwrite each
other's gesture or hold timer. Do not move stream fields
back into node-wide data and do not create one node per pad as a workaround.

The stream array is sized from enabled input-listener instances. An invalid
runtime index is passed through without logging in the event hot path; it must
never alias stream zero.

## Event contract

1. Buffer `INPUT_ABS_X` and `INPUT_ABS_Y`, clamped to the configured range.
2. Treat only the off-to-on `INPUT_BTN_TOUCH` transition as a new contact.
   Repeated touch-on reports must not restart the gesture.
3. On the first synchronized report with touch active and both coordinates
   available, latch the start position and optionally schedule long press.
4. Compare squared Euclidean travel with the squared flick threshold. The
   first qualifying movement cancels long press. Continue tracking and use the
   direction at the farthest point, not the first threshold crossing.
5. On release, report a held-cell release, a latched flick, or a tap. Clear all
   buffered contact state so a later touch cannot inherit stale coordinates.
6. Preserve every original KEY type/code/value/sync for the final filter. A
   generation reset may neutralize stale coordinate payload while preserving
   sync; it must never swallow an original button edge. Matrix owns generated
   virtual keys, not source mouse-button disposition or pending HID frames.

Source-output policy belongs entirely to the final filter. Do not add a
filter device reference, suppression setting or filter API call to Matrix.
`enabled` controls gestures only. Composition fixtures configure the final
route masks separately and assert that Matrix runtime edits cannot change them.

## Hold and reset races

Every contact has a monotonically changing `contact_id`. Delayed hold work may
cross a release, new contact, layer change, or settings update. The handler and
event path use `hold_reported`, `hold_release_pending`, and contact IDs so any
press that escapes is paired with exactly one release and cannot clear a newer
contact.

Node-wide `reset_generation` is atomic. Event processing records it on entry
and checks it again immediately before cancellation, scheduling, or KSCAN
output. If it changed, the event is discarded and the stream is resynchronized
instead of mixing old and new settings. `applied_generation` is atomic too;
do not replace it with an unlocked plain integer.

An already claimed release for a reported hold must still be emitted exactly
once, even when the rest of the event is discarded. Re-read the generation
after dispatching that release: its callback can change settings or layers.
Every original KEY edge must survive that discard; the final owner applies its latched disposition.

A layer change and a runtime settings update both resync gesture state:

- release any reported hold;
- cancel pending delayed work;
- clear touch, coordinates, start and flick state;
- increment the contact ID;
- apply the current generation.

Runtime/layer resets cancel gesture state and release generated claims. Source
button histories and pending report boundaries belong to the independent final
owner. Place the same final instance on every listener route. Matrix itself
appears only on gesture routes and observes no bypassed source events. Its
explicit companion integration tests assert complete source pairs, exact HID
frames, repeat handling, overlap independence and first-click recovery. The
stricter historical click checks remain executable and must be rerun after an
extraction; do not mark old failure evidence resolved without those results.

Contact reduction updates contact state and returns effects; effect application
dispatches KSCAN/work operations. Keep their release/cancellation assertions
here. Transferred filter assertions and intentional unmatched-release/polarity
changes are mapped in tests/README.md.

KSCAN reporting and work cancellation/scheduling must remain outside stream
spinlocks. Runtime parameter copies are protected by their own spinlock so the
event path always receives a coherent parameter snapshot.

## Performance rules

The coordinate hot path must remain bounded and allocation-free. It may use
fixed-width integer arithmetic, spinlocks, and atomic loads, but must not:

- allocate memory;
- block or sleep;
- perform settings I/O;
- scan settings descriptors;
- perform logging in the normal successful path;
- put KSCAN reporting inside a spinlock.

Keep queue callback/coordinate words separate from the edge-polarity array;
this avoids padding one bool per edge without narrowing public coordinates or
reducing capacity. Acquire/release transfer their already-held output lock to
the drainer, which always returns unlocked. No nested lock acquisition or
callback under that lock is allowed. An empty/already-draining queue only
unlocks and returns. Preserve the final-release reservation and FIFO ordering.

The optional owner CPU comparison in `tests/README.md` measures the same host
fixtures before/current; it does not establish real kernel, Cortex-M, input
latency or power performance. Report every scenario, including regressions or
inconclusive differences, plus the real production linker footprint.

Runtime settings are re-read only when a settings event occurs. The custom
settings listener may scan the small instance set because it is not in the
input-event hot path.

## Build integration

Sources are attached to ZMK's `app` target. Keep this arrangement: current
upstream ZMK does not expose all application headers to an independent Zephyr
module library, while both upstream and DYA builds expose them to `app`.

The custom-settings source is compiled only when
`CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS` is enabled. The base module must
continue building without the DYA custom-settings dependency.

## Tests and release gate

`.github/workflows/test.yml` builds the integration fixture against:

- upstream `zmkfirmware/zmk` `main`;
- the DYA fork's `main+dya` with custom settings enabled.

The fixture deliberately connects two input listeners to one processor node.
The DYA build also checks that the subsystem and representative setting keys
are linked into the firmware.

Before release or pull request:

1. Run `git diff --check`.
2. Run both integration variants:
   - `bash tests/run-integration-docker.sh upstream`
   - `bash tests/run-integration-docker.sh dya`
3. Build at least one real central, peripheral, and settings-reset target when
   the downstream keyboard provides them.
4. Inspect compiler output for warnings originating in this module.
5. Confirm no keyboard name, user name, serial number, local path, or hardware
   identifier has entered source, docs, fixtures, or artifacts.
6. Keep README English/Japanese instructions and the MIT license current.

Do not claim public readiness if either upstream or DYA integration fails.

## Repository hygiene

- Preserve SPDX headers and LF line endings.
- Keep names generic; examples and fixtures must not depend on a private board.
- Do not commit build products, logs, temporary directories, or generated UF2
  files.
- Do not change public binding names, setting keys, subsystem ID, or gesture
  ordering without an explicit migration plan.
- Do not commit, push, tag, publish a release, or open a pull request unless the
  current task explicitly authorizes it.
