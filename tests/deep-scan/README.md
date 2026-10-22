# Matrix native ownership and IQS regressions

Set ZMK_TEST_SUPPRESS_ROOT explicitly. All default/child routes use the same
final instance and direct masks; it has no profile table or layer watcher.
The source-local contact mask survives lift until a fresh distinct touch.
Matrix runtime parameters cannot change that output policy.

Run in the documented Linux build image with an explicitly resolved workspace
and module environment. Both supported cores stay unchanged:

```sh
ZMK_TEST_CORE=dya python3 tests/deep-scan/run.py /resolved/dya /fresh/dya \
    --iqs-root /resolved/iqs
ZMK_TEST_CORE=upstream python3 tests/deep-scan/run.py /resolved/stock /fresh/stock \
    --iqs-root /resolved/iqs
```

Each command builds once and runs **46 Matrix cases plus 17 IQS cases** three
times with the same binary (63 cases). The combined integration entry accepts
the producer path through `ZMK_TEST_IQS_ROOT`. `--iqs-only` selects the 17 IQS
cases. Without `--iqs-root`, only the 46 Matrix cases run and stdout/selection.json
record IQS NOT RUN. The old six synthetic click cases and empty legacy route
selector have been removed after source/failure archival and assertion mapping;
their checks live in the corresponding actual-producer cases below.

Default pins are DYA ZMK `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`, stock
ZMK `5b51501fead672c41b5cfb396f3dafe0894bf4e9` and Zephyr
`10ba6d0cb38bc3d258775d27982f707599320085`. The integration entry supplies its
manifest-resolved core SHA with `--core-revision`. Unknown cores and modified
core/Zephyr checkouts are rejected. No dependency is fetched or patched.

Runs reject existing output paths, use fresh temporary build storage and retain
commands, source/dependency identities, binary hash, config, DTS, compiler inputs
and raw ordered output. Source and producer identities are compared before/after.
No snapshot is accepted to clear a failure.

## Matrix boundaries

The shared-cell contract is the union of admitted source claims: first owner
presses, final owner releases, and a tap over an occupied cell adds no edge.
The 46 cases cover two streams/processors, independent cells, actual keymap/HID
state, reset/layer cleanup, callback reentry, a concurrent real-kernel producer,
capacity-independent lifecycle behavior and a 300-step seeded walk.

`lifecycle_cases.h` covers paired suppression across retained/departed routes,
repeat presses, negative values, touch aliases, virtual hold cancellation and
pending frame synchronization across KEY/ABS suppression and settings/layer
resets. Retained-route, reset and consumed-frame checks have archived failing
before-repair evidence. Direct raw-event and real input API checks are separate:
`input_report_key` normalizes every nonzero value to ON, so a -1 helper input is
another down rather than a negative release.

`matrix-key-isolation` sends BTN_0, BTN_4 and BTN_TOUCH through the real listener,
then verifies that a coordinate/touch contact creates one virtual press/release
without a mouse report. Repeated forwarded downs, independent mouse owners and
frame endings retain their distinct assertions. Direct ON/ON/OFF is a generic
input boundary, not normal IQS tap generation.

Layer1 and the default fixture route select Matrix; layer2 is the pointer route containing only the shared final filter. Unrelated layer3 tests virtual-key cleanup without route departure.
The required acceptance is release safety for balanced source ON/OFF pairs:
all scheduled edges complete, final mouse/virtual-key state is released, and
the next pointer click remains usable. Early release during a route transition
is permitted and logged; preserving exact click duration/source independence
is an additional strict requirement. This is an intentional acceptance change,
not a repair of the old early-release observations.

The shared final filter consumes unmatched ups and latches the first press
disposition through its release across every route. The common-filter host fixtures cover all16
codes, zero/negative ups and independent streams. BTN_TOUCH remains internal
while active except for this stream's observed forwarded pair.

## Actual IQS click producer

`iqs_source.c` includes the selected complete production `src/iqs7211e.c`
unchanged, plus its unchanged filter source. It uses the actual data structs,
private queue, queue_clicks, delayed click handler and old-click release function.
Duplicate cached registration is excluded and recorded so the selected producer
is compiled once. No copied algorithm or source-rewriting adapter supplies it.
Assertions and scenarios stay in Matrix's tests.

The gesture-to-click handoff runs on the actual driver queue at priority -1
with stack1536. A wrapper records the real input_report call's producing thread,
value and time, then delegates to the unchanged Zephyr input queue. The first
new ON waits for the current producer work to return. Later edges alternate
ON/OFF with intervals at least20ms. A replacement tap immediately closes an
outstanding old ON and queues its own ON; that old OFF is exempt from the normal
spacing bound.

| Cases | Assertions |
| --- | --- |
| iqs-pointer / iqs-matrix | Single click: exact HID [1,0] or no mouse output; complete OFF |
| iqs-pointer-to-matrix / iqs-matrix-to-pointer / iqs-pointer-return | Route changes after ON and before OFF; exact ordered HID and final OFF |
| iqs-overlap | Complete both OFFs, HID [1,0-or-1,0], ordered source times and a successful recovery [1,0] pair; strict mode additionally requires [1,1,0] |
| iqs-double-pointer / iqs-double-matrix / iqs-triple-pointer / iqs-triple-matrix | Four/six alternating source edges, spacing and exact HID traces |
| iqs-replace-pointer / iqs-replace-matrix | Old ON closes before the newly queued ON; both pairs finish |
| iqs-before-on-pointer-to-matrix / iqs-before-on-matrix-to-pointer | Hold current producer work after enqueue, switch route before first ON |
| iqs-double-pointer-to-matrix / iqs-double-matrix-to-pointer / iqs-triple-pointer-return | Later pairs resolve the current route independently |

All producer OFFs execute before final-state and ordered HID assertions.
The first six IQS cases preserve the retired synthetic click source-order,
completion and final-state assertions, including accepted ON < left OFF < right
OFF and final HID OFF at/after the accepted source's OFF. The original overlap
duration/source-independence assertion remains executable in strict mode. The former synthetic producer is no
longer an alternate implementation or run mode. Three additional subset cases
for independent cells, shared holds and tap-over-hold were removed: each had
the same inputs and KSCAN assertions as its retained real-HID counterpart.
The retained cases assert the complete KSCAN trace/state plus HID state; no
separate output-only run adds an assertion.

## Release safety, strict observations and negative controls

The required selection has46 Matrix plus17 actual IQS cases (63 total), three
identical-binary runs per core. `route-forwarded-pairs` uses complete ON/OFF
pairs across route changes and checks release/recovery. The old ON/ON/OFF case
is not a normal IQS sequence and remains executable in the strict selection.
The suppressed-release/touch-alias cases now execute both OFFs and a fresh
click before judging the result. The overlap also executes a real IQS recovery
pair. Early release is printed as CLICK OBSERVATION, not hidden.

To retain the former stricter assertions separately:

```sh
ZMK_TEST_CORE=dya python3 tests/deep-scan/run.py /resolved/dya /fresh/strict \
    --iqs-root /resolved/iqs --strict-clicks
```

Repeat on stock. This selects only the five affected checks, avoiding another
run of unrelated scenarios. It sets MATRIX_STRICT_CLICKS and requires PASS for
every selected assertion. Current direct-mask composition requires PASS in all
five checks. Before extraction, the command failed early-release, repeated-ON
and stale reentry assertions. Those original sources and three-repeat failures
remain externally archived; they are not accepted negative controls. The final
shared history now retains source claims across routes and rejects late paired
presses. The touch-alias fixture explicitly checks that a delayed gesture tap
stays silent until a new touch, then the first fresh click succeeds. Prior sources/failed traces are archived; the current delayed-tap and fresh-touch
assertions remain in the required selection.

Three genuine negative controls remain:

```sh
ZMK_TEST_CORE=dya python3 tests/deep-scan/run.py /resolved/dya /fresh/no-matrix \
    --negative-control inactive-matrix
ZMK_TEST_CORE=dya python3 tests/deep-scan/run.py /resolved/dya /fresh/inline-on \
    --iqs-root /resolved/iqs --iqs-only --negative-control iqs-inline-first
ZMK_TEST_CORE=dya python3 tests/deep-scan/run.py /resolved/dya /fresh/no-off \
    --iqs-root /resolved/iqs --iqs-only --negative-control iqs-drop-release
```

The first actually omits Matrix and must fail matrix-key-isolation. The second
calls the actual click handler inline and must fail both before-ON assertions.
The third records the actual producer OFF but intentionally omits its delivery
to the input queue, and must fail the final held-button assertion. Three repeats
verify each named failure. Only explicitly selected controls expect failure.

## Evidence limits

The IQS boundary starts after gesture recognition. It does not exercise I2C/RDY,
full BTN_TOUCH/ABS frames, production abs2rel chains, PM/failure retries or
separate peripheral queues and split transport. Both producers share this native
process's actual driver queue. Settings/Bluetooth are off in this base fixture.
Keymap checks use the public position API after the recording callback rather
than the downstream physical-layout queue. Endpoint snapshots are send-call
state, not physical host arrival. Arbitrary interleavings, sensor/USB/BLE/split
delivery, latency, power and board acceptance remain NOT RUN. Run the owner's
host and supported-core firmware/kernel entries separately; ZMK core is unchanged.
