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
| Source allocation | test_source_limits.py: complete driver at automatic2/5, limited1/2/256 and no-listener fallback; ASan/UBSan, full optimized/m32; integration/source-limits.py:42 actual generated-DTS controls, including sparse IDs, every route and invalid bounds; source-limits/run.py:actual listener IDs0/1 and3/4 with real holds/settings/release, three same-binary repeats/core |

Quick runs every host scenario under ASan/UBSan. Full adds optimized/coverage
and retained ABI variants. Real function ranges require executable lines and
complete branches; macro expansions are measured in callers. No behavioral
assertion was dropped. KSCAN reporting/work operations remain outside stream
locks; contact generations/pending releases and output claim/FIFO state are
necessary generated-key behavior, independent of final source suppression.
The unused coordinate constant and logger registration in the silent processor
are removed; active KSCAN/settings logs and their Kconfig level remain.

## Dependencies and evidence

Read root AGENTS.md (AGENTS.ja.md is its Japanese translation) and resolved dependency instructions first.
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

## 32-bit arithmetic and clock contract

Normalized geometry uses bounded uint32 products. Squared travel keeps one explicit carry bit alongside its uint32 low word, retaining exact farthest-point ordering up to the maximum supported coordinates. Hold deadlines use modulo uint32 uptime and explicit armed state, including deadline zero after wrap. Pending deadlines assume elapsed observations below 2^31 ms.

runtime/geometry_harness.c retains exact large squared-distance and direction/tie assertions using a test-only wide reference. runtime/driver_harness.c adds carry ordering and wrapped/zero/unarmed hold deadlines. Existing generated-key, reset and settings/race assertions remain.

## Native-width hold timeout conversion

The variable hold-work reservation uses the private `src/matrix_timeout.h`
conversion. The full uint16 range of 0..65535 ms, exact ceiling, zero-disable
behavior, contact deadlines, generations, claims/releases and settings/layer
cancellation remain unchanged. Clock rates through 65536 Hz use a bounded
uint32 numerator. Higher rates split seconds and fractional products while
retaining the kernel tick ABI, without a software 64-bit divider or a new clock
restriction.

The canonical host entry includes `test_timeout_width.py`. It executes every
uint16 interval at 14 rates against an independent wide ceiling oracle with
both 32/64-bit tick forms. Full retains optimized, ASan/UBSan, measured nonzero
100% header line/branch coverage and m32 numeric/object variants. Four semantic
faults each must compile and produce qualified C assertion failures three times
per tick form. A numerically correct wide-divider control must be rejected by
the object-width guard. No existing behavioral assertion is removed.

The existing real-kernel fixture additionally compares the private production
header with pinned Zephyr 32/64-bit conversions at 7 positive rates for every
uint16 interval. It retains all hold/settings/layer groups, ordered output and
three repeats per supported core. This verifies calculation and named kernel
boundaries; physical HID/split/sensor timing and power remain unmeasured.

```sh
python3 -m unittest discover -s tests -p test_timeout_width.py -v
```

## Optional source allocation

The automatic default (`CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES=0`) remains one
stream per enabled listener, with at least one. A positive bound reserves
original global IDs0..bound-1; sparse use never becomes a dense index. The guard
matches the core's enabled source-device gate, the listener base route and all
children, including disabled children compiled by `DT_INST_FOREACH_CHILD`.
Runtime NULL/invalid behavior and all gesture/claim/reset/cancel/settings
assertions remain. Host allocation tests run the complete driver for each
allocation with two or more slots; one-slot/fallback cases retain explicit
NULL, highest, invalid and settings reset probes. The256-slot case retains all
uint8 IDs and therefore has no impossible out-of-range runtime comparison.

Both canonical integration variants run the42 generated-DTS controls and three
native profiles: automatic five listeners with pads0/1; bound2 with those same
listeners; automatic five with actual pads3/4. Each binary executes three times.
A fourth native build must reject the higher IDs under bound2 with the named
source-limit diagnostic. No existing Matrix/IQS/strict/negative case is replaced.
The generated-DTS runner's `--negative-control skip-routes|skip-children` must
fail for its named missing sparse-reference rejection; these are test controls,
separate from positive product results.

For the complete limited profile after a successful canonical integration run:

```sh
python3 tests/source-limits/complete.py upstream COMPLETED_INTEGRATION /fresh/limited
python3 tests/deep-scan/post.py upstream COMPLETED_INTEGRATION/workspace \
    COMPLETED_INTEGRATION/ownership /fresh/strict-and-controls
```

Repeat with `dya` and its completed integration. Keep the explicit final-filter
and IQS provider environment selections. The first entry runs all63 cases and
five strict cases at bound2, reusing one binary; it runs every original kernel
group at bound2 and separately classifies a whole-kernel ASan/UBSan configuration.
The second reuses the original full binary for five strict cases and dispatches
the three genuine defect controls through the existing owner entry. Every case
has three same-binary repeats. A named unchanged-core sanitizer fault is BLOCKED,
not an owner PASS; unclassified failures reject the entry.

The original Matrix fixture leaves the native board's independent mock KSCAN
unchosen and unconfigured. Its sanitizer PASS does not exercise that decoder.
`python3 tests/kernel/mock-boundary.py upstream|dya COMPLETED_INTEGRATION
/fresh/mock` explicitly configures/enables the unchanged mock with an immediate
nonempty event, alongside all original kernel groups under ASan/UBSan. It requires
both completion markers for PASS and retains a named unchanged-core signed-shift
failure as BLOCKED. It does not patch the core, empty the event list or disable
sanitizers. Repeat every binary3/core. Physical KSCAN/electrical acceptance is
still separate.

Set `ZMK_TEST_BUILD_JOBS=1` to keep every firmware/native compile within a
one-worker budget; the default remains2. The selected workspace helper rejects
invalid worker counts before project discovery. Runtime packages/toolchains and
all builds stay inside Docker. Allocation sizes, compiler frames and actual
installed stack high-water are different observations. Production still needs
the complete consuming role builds and physical HID/split/power acceptance.

## Atomic generation wrap

`test_generation_wrap.py` drives the actual setter and layer-reset functions at
LONG_MAX→LONG_MIN and -1→0. It checks the release of an active hold, every stream's
applied generation and a replacement contact's complete hold pair. The atomic
stub models Zephyr's defined atomic wrap; the driver computes its own next token
with width-matched unsigned arithmetic, without an extra atomic read. Quick runs
both paths3/binary with ASan/UBSan. Full adds O2 and 32-bit UBSan, preserving all
other driver cases. The original signed expressions produce a qualified UBSan
overflow at both word widths3/path; optimized apparent success is not UB safety.
`ZMK_TEST_GENERATION_OUTPUT` optionally retains rendered source, commands, binary
hashes and raw repeats at a fresh absolute evidence directory.
