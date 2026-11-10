# zmk-input-matrix: agent guide

English is the authoritative guide. [日本語版](AGENTS.ja.md) contains the same rules; update both
together.

## Scope and contract

- Own `zmk,input-processor-matrix` and `zmk,kscan-input-matrix`. Preserve bindings, settings keys,
  subsystem ID and gesture order; changes require an explicit migration plan. Normal geometry is
  columns by rows * 5 with Tap, Up, Down, Left, Right blocks; diamond mode is processor/KSCAN 1x4,
  taps only, Up/Right/Down/Left, vertical ties and Down at exact center. Split peripherals forward
  raw input; the central processes gestures.

- Runtime updates change only `enabled`, `flick_threshold` and `long_press_ms`;
  geometry/ranges/output/diamond are compile-time. Accept only this driver's devices, reject zero
  flick threshold, replace parameters atomically and advance reset generation/resync on success.
  Zero long-press disables holds.

- Configuration/output selection are node-wide; coordinates, touch/flick/hold state, maximum
  travel/direction, delayed work, contact IDs and applied generation are per `input_device_index`.
  Size streams from enabled listeners; invalid indices pass through without aliasing stream zero.
  Shared listeners must not share histories or be replaced by one node per pad.

- Virtual KSCAN owns the union of claims: first press/final release only, no extra edge for tapping
  an occupied cell, no release of failed acquisition. Preserve bounded claim/edge capacity and
  atomic admission reserving final release. One FIFO drainer orders concurrent/reentrant output;
  callbacks stay outside locks. Never mix raw `report_event` with claim APIs for one cell.

- Clamp buffered X/Y. Only off-to-on touch starts a contact; the first synchronized touched report
  with both axes latches origin/hold. Compare squared Euclidean travel, cancel hold on first flick
  qualification and retain direction at farthest travel. Release emits held release/flick/tap then
  clears coordinate buffers. Preserve original KEY type/code/value/sync, including generation
  discards. The final filter owns source buttons/pending HID frames: add no filter
  references/APIs/settings to Matrix. `enabled` controls gestures only; runtime edits never change
  final masks or infer bypassed edges.

- Contact IDs change on every contact/reset. Delayed holds use contact ID, `hold_reported` and
  `hold_release_pending` so an escaping press receives exactly one release without clearing a
  replacement contact/deadline. Keep node/applied generations atomic; check on event entry,
  immediately before cancel/schedule/KSCAN effects and after callbacks. Layer/settings resync
  releases holds, cancels work, clears touch/coordinates/origin/flick, advances contact ID and
  applies current generation; original KEY edges survive. Keep work/KSCAN effects outside stream
  spinlocks and parameter snapshots coherent.

- With `CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS`, register `amgskobo__matrix` per processor using
  real node names. The registry alone owns persistence; keep live state local. Retain compile-time
  bounds for RPC keys and full `custom_settings/<subsystem>/<key>` storage names. Boot applies every
  instance; edits affect only their owner. Serialize the whole registry read/apply transaction to
  prevent stale overwrites. Read settings on settings events, never per coordinate. The base module
  still builds without this adapter.

- Keep coordinate work bounded/allocation-free: no blocking, sleep, settings I/O, descriptor scans,
  normal-path logging or KSCAN callbacks under spinlocks. Keep callback/coordinate words separate
  from edge polarity, coordinates full-width and output capacity unchanged. Claim acquire/release
  transfers its lock to the drainer, which returns unlocked; empty/already-draining queues only
  unlock/return, with no nested locking. Attach sources to the ZMK app target and conditionally
  compile adapters.

- Preserve the documented 32-bit geometry/travel and hold-clock contracts. Squared travel retains
  its carry for exact farthest-point ordering; hold deadlines use modulo uint32 uptime with explicit
  armed state, including deadline zero after wrap. Pending elapsed observations stay below 2^31 ms.
  Keep the full uint16 hold-time range, zero-disable behavior and native tick-width conversion
  checks; do not narrow coordinates or add a clock-rate restriction.

- Retain shared-listener, claim overlap/reentrancy, hold/reset races, settings/registration,
  producer/final-filter composition, complete pairs/ordered frames/repeats, first-click recovery and
  strict-click controls. Read `tests/README.md` for stock/DYA and native/deep-scan selections.
  Downstream integration changes require affected central/peripheral/settings-reset builds and
  linked-feature/warning checks. Keep generic examples, MIT/SPDX headers and LF. Host CPU
  comparisons retain every scenario and actual linked footprint; they do not establish Cortex-M
  latency or power.

- This owner supports both unchanged stock ZMK and DYA. Keep its documented dual-core and
  enabled/disabled adapter/role checks when the changed boundary requires them.

- Reading map for relevant changes: [README.md](README.md), [README_JA.md](README_JA.md),
  [tests/README.md](tests/README.md), [tests/maintenance.json](tests/maintenance.json),
  [tests/contract.json](tests/contract.json),
  [processor binding](dts/bindings/zmk,input-processor-matrix.yaml),
  [KSCAN binding](dts/bindings/zmk,kscan-input-matrix.yaml),
  [runtime API](include/zmk-input-matrix/matrix_runtime.h). Read the affected contract/coverage
  sections; documentation-only edits do not require unrelated dependency tours.

## Working rules

- Read the request and existing diff; preserve unrelated and staged work. Finish authorized
  inspection, edits and reversible checks without repeated approval. Ask only for a missing decision
  or an out-of-scope action. Before a destructive rewrite, preserve irreplaceable uncommitted
  content as private evidence, not rolling whole-tree copies.

- Git staging, commits, pushes, PR changes, merges, tags, publication, flashing and settings/bond
  erasure need authorization for that operation. Existing authorization remains valid for the same
  operation and scope. Dependency guides and passing checks grant none. Before an authorized commit,
  run applicable pre-commit hooks in Docker without bypasses; keep the configured Git identity.

- Use the selected checkout and manifest/lock sources; do not silently switch branches, reset
  history, replace remotes or create authoring copies. Before changing selection, verify source
  URLs/full commits, review dependency changes, update affected pins/locks and run owner/integration
  checks. Distinguish local working snapshots from remotely retrievable commits; report missing refs
  rather than inventing them.

- Read relevant root/nested owner and dependency instructions once per selected checkout/revision.
  Resolve actual west imports, local overrides, enabled features and backend; sibling
  directories/cache names do not identify inputs. Record READ, NO AGENTS or INACCESSIBLE when
  preflight requires it. Missing inputs block dependent checks; continue independent authorized
  work.

- Keep permanent agent rules in this synchronized EN/JA pair. Usage/API details stay in their
  existing documentation homes. Do not add agent-guide fragments or private shared-guide
  dependencies. Remove duplicated rules and obsolete task/branch assumptions without discarding
  active contracts.

## Execution environment

- Run local automated test/build runtimes and dependency setup in Docker; a documented DevContainer
  qualifies. Unless stated otherwise, work from the owner root in its resolved workspace. Launch
  Docker-wrapper scripts on the host; run their inner Python/Node/compiler entries inside the
  prepared container without wrapping Docker again. Host source/Git/reference checks and evidence
  export are allowed; do not install test packages/toolchains on the host. Keep dependencies,
  runtime/build trees and caches in images or named volumes.

- Separate preparation and deliberately selected online compatibility checks from ordinary
  network-free tests. Inspect the available immutable image ID/digest and verified
  dependency/toolchain revisions; pass the image through the owner's declared image option; honor
  declared local overrides and record working files. A launcher that installs or fetches
  dependencies belongs to preparation/online checking, not an offline run. Do not silently pull,
  advance pins or mutate shared caches, patch dependencies or disable required features to clear a
  gate. Missing inputs block dependent checks only; documented preparation within an authorized
  build/setup remains in scope.

- Reuse compatible environments/caches, retaining required clean-cache lanes. Provide required
  inputs, including relevant untracked files, through read-only mounts or isolated container copies;
  verify source identity before/after. Isolate mutable build/runtime/install trees per job under one
  CPU/memory budget, including nested workers. Build once per configuration and reuse that exact
  binary for identical repeats; cached PASS is not a new run. Performance comparisons separate
  preparation/staging/build/execution/validation and cold/warm conditions. Existing hosted platform
  jobs retain their own OS requirements.

## Checks and entry points

- Documentation/instructions only: run `git diff --check`, verify changed paths/commands and EN/JA
  agreement. Without Git history use `git diff --no-index --check` on before/after copies. Firmware
  suites are not required solely for prose changes.

- On the host, portable behavior uses `bash tests/run-docker.sh`; full mode is
  `ZMK_TEST_MODE=full bash tests/run-docker.sh`. Select an already inspected immutable image with
  `ZMK_TEST_IMAGE`. These scripts launch Docker; inside that prepared container use
  `bash tests/run.sh` with the same `ZMK_TEST_MODE`. Keep the complete affected matrix for
  interface/infrastructure changes; quick is not a coverage report.

- For public API/DTS/Kconfig/registration/settings or kernel changes, use the offline inner entry in
  the prepared image with this owner mounted at `/src`. Set `ZMK_TEST_OFFLINE_WORKSPACE` to the
  selected resolved core and `ZMK_TEST_OUTPUT` to a fresh absolute container path. Run
  `bash tests/integration/run.sh upstream` and `bash tests/integration/run.sh dya`, selecting the
  matching workspace and a distinct output each time. Retain native/negative controls. The legacy
  `tests/run-integration-docker.sh` may perform online setup and does not forward the full offline
  selection; do not assume host variables pass through Docker.

- For the DYA offline variant also supply `ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE` with the exact
  revision/digest admitted by `tests/integration/offline.py`. A current working registry override is
  separate evidence and does not replace this pinned-dependency check. Prepare missing archives
  explicitly; do not fetch them during ordinary tests.

- The composition fixture requires an explicitly resolved final-filter owner in
  `ZMK_TEST_SUPPRESS_ROOT`; this fixture relationship does not add a production settings/API
  dependency.

- For complete producer composition, additionally set `ZMK_TEST_IQS_ROOT` to the resolved IQS7211E
  owner and `ZMK_TEST_REQUIRE_IQS=1`. A run without the required producer is a focused subset, not
  the full producer matrix.

## Test quality

- Keep module fixtures, assertions, independent oracles, mutations, selectors and validators in the
  owner's tests/; retain documented Web and non-module test layouts. CI/package scripts dispatch
  owner entries, not sibling/private suites or source-rewriting substitutes. Probes expose state,
  not scenarios. Map implementation families to normal/boundary/invalid, independent-state,
  ordered-time/output, reset/cancel, failure/recovery and role/protocol cases and explicit gaps.

- A regression needs a failing original or named control. Expected-failure controls qualify only by
  their specified assertion/rejection; report them separately from product results. Do not weaken
  assertions, accept unclassified snapshots or count compilation errors as killed runtime faults.
  Before retiring tests, archive unique original assertions/failures externally with a replacement
  map. Quick runs every behavioral scenario with sanitizers where supported; full adds the
  documented ABI, optimization, coverage, kernel and supported-core matrix.

- Use focused checks for local implementation changes and the complete affected owner matrix for
  interface/test-infrastructure changes. Run unchanged stock ZMK and DYA only where both are
  supported. DYA-only features are NOT APPLICABLE to stock; disabling them is not compatibility
  evidence. For test suites, verify the required nonempty case/fixture completions; a zero exit
  alone, skipped required cases, timeouts or setup errors do not establish PASS.

## Evidence and handoff

- Use fresh evidence paths: resolve parents and reject existing files, directories or symlinks.
  Record commands/exits, source/dependency/config/backend/image identities and raw ordered
  output/times, retaining first failures and relevant config/DTS/binaries. Source or dependency
  drift invalidates affected results; never edit running scripts. Put retained outputs on a mounted
  named volume or export them before container removal, including failures; a path printed inside a
  disposable container is not retained evidence.

- Preserve exact fixed-input traces or specification-based timing bounds; totals do not replace
  per-report order/time. Before calling a mismatch nondeterministic, repeat identical
  binary/input/seed/backend/initial-flash conditions and PHY where applicable at least three times.
  Host load alone is not an explanation. Distinguish invalid/stale/incomplete/unavailable
  observations from valid zero.

- Report PASS, FAIL, BLOCKED, NOT RUN and NOT APPLICABLE distinctly. Local CI, host/native/compile
  and simulator results do not establish hosted CI, browser or physical
  HID/BLE/split/electrical/power/reset acceptance. Validate exercised production paths and correlate
  hardware before simulator-fidelity claims. End-to-end/stability claims need matching
  installed-image and host observations; quiet logs are not proof.

- Before sharing, review source/comments/guides/examples/logs/media/artifacts/metadata for
  credentials, personal/host/device IDs, private paths, bonds and individual histories/preferences.
  Authorized local work may use real values; keep private originals and label sanitized copies. Use
  clearly synthetic public fixtures/placeholders; preserve public project/API identifiers, licenses
  and attribution. Keep useful diffs/evidence. Handoff reports behavior, files, actual checks,
  gaps/risks and version-control/device actions.
