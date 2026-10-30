# ZMK Input Matrix (zip_matrix)

[![Test](https://github.com/amgskobo/zmk-input-matrix/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-matrix/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[日本語](README_JA.md)

A ZMK Input Processor that converts trackpad absolute X/Y coordinates into a configurable gesture grid with long-press support. Gestures are reported as standard KSCAN matrix events for full ZMK Studio compatibility.

## Compatibility

The module is continuously compile-tested against both upstream ZMK `main`
and the DYA fork's `main+dya`. Core gesture processing works on either tree. The
runtime-settings UI is optional and is enabled only on a tree that provides
[`zmk-feature-custom-settings`](https://github.com/cormoran/zmk-feature-custom-settings), such as `main+dya`.

## Features

- **Dynamic Grid**: Configure any grid size (e.g., 1x1, 2x2, 3x3)
- **Block Layout**: Each gesture (Tap/Up/Down/Left/Right) gets a full block stacked vertically
- **Synchronized Latching**: The first complete coordinate pair after touch-on is latched on an input event with `event->sync` set, establishing a stable start position
- **Long-Press Support**: Configurable tap-hold duration (set `0` to disable)
- **Layer-Change Safe**: A held cell is released, a pending hold cancelled, and the contact closed out, when the layer changes out from under it
- **Split Keyboard Ready**: Optimized for central-side processing
- **Thread-Safe**: Uses spinlocks to prevent race conditions

One processor node may be shared by multiple ZMK input listeners. Configuration
and the output KSCAN device belong to the node, while contact coordinates,
flick detection, hold work and contact generations
belong to `input_device_index`. Local and split-proxied pads therefore cannot
overwrite each other's in-progress gesture state and do not need duplicate
matrix processor nodes. An invalid listener index is passed through and never
aliases stream zero.

The delayed long-press handler performs only bounded in-memory state changes
and KSCAN reporting: it does not sleep, allocate, scan settings, or write
flash. Contact generations and pending-release bookkeeping reconcile a release
or settings change that arrives while a hold is being admitted.

When inputs share a virtual output cell, the first active source presses it
and the final source releases it. A tap over an already held cell leaves that
cell held. This applies across processor nodes as well as listeners. Independent
cells retain their own output order. The bounded ownership API defaults to 16
simultaneous claims and 32 pending/reserved edges per output; adjust
`CONFIG_ZMK_INPUT_MATRIX_MAX_CLAIMS` and
`CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE` for a larger graph. A rejected new
claim produces no partial press; an accepted claim retains its final release
capacity. Callback disable/replacement does not promise held-state replay or
cleanup of the previous consumer.

## Runtime settings

Delayed holds belong to a contact and its deadline. A running callback from an
older contact cannot hold its replacement before the replacement's own deadline,
or after long press is disabled. At boot, stored settings apply to every node;
an individual edit applies only to the node owning that setting and leaves peer
contacts and held keys intact.

With `CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y`, DYA Studio exposes each
instance under `amgskobo__matrix`. The editable values are `enabled`,
`flick_threshold` and `long_press_ms`. The registry owns persistence. A settings
update cancels gesture work and releases generated holds. Original source
buttons pass unchanged. Matrix has no dependency, phandle or API call to an
output filter; gesture enable and source suppression are independent settings.

`rows`, `columns`, `kscan`, and `diamond-tap` remain devicetree-only because
they define the virtual matrix and keymap layout rather than live tuning.

Setting keys begin with the devicetree node name. Keep custom instance node
names short enough for Zephyr's persisted-settings limit. The module checks
both the RPC key and full storage name at compile time, so an unsafe name fails
the build instead of accepting a value that cannot be saved.

### Gesture layer routes and final policy

Place Matrix only on gesture routes. Append the same independent
[`zmk-input-suppress`](https://github.com/amgskobo/zmk-input-suppress) instance to every pointer, scroll and gesture route:

```dts
#include <zmk-input-suppress/input_processor_suppress.dtsi>
&trackpad_listener {
    input-processors = <&zip_absolute_to_relative>,
                       <&input_suppress (SUPPRESS_TOUCH | SUPPRESS_ABS)>;
    gestures {
        layers = <1>;
        input-processors = <&zip_matrix>, <&input_suppress SUPPRESS_ALL>;
    };
};
```

Matrix observes coordinates/touch, emits virtual-KSCAN claims and passes source
buttons unchanged. Layer events release virtual holds and cancel pending work
even after its route is bypassed. The final filter observes every source edge,
retains its first-press disposition across transitions and releases only claims
it admitted. It also owns pending/silent mouse-frame normalization. A filtered
pair stays filtered after leaving Matrix; a pointer pair can finish after
entering it. Missing source events/lost connections require a separate cleanup
contract; neither module invents a timeout or resets global HID state.

### Migration

The former source-button histories, frame state and suppression settings now
belong to the independent final owner. Remove `suppressor`,
`suppression-profile`, `suppress-abs`, `suppress-key` and `suppress-btn-touch`
from Matrix nodes. Remove the runtime `suppress_abs` field. Set residual-ABS
and button masks on the route's final-processor argument instead. Matrix's
`enabled` controls gestures only; disabling gestures does not enable mouse
clicks. Select that output behavior explicitly at the final owner.

Old `amgskobo__matrix/<node>.suppress_abs`, `suppress_key` and
`suppress_btn_touch` stored entries are ignored without erasure. This is an
explicit schema migration; there is no compatibility link or automatic rewrite
of existing stored values. Structural gesture/KSCAN APIs are unchanged.
See [the assertion map](tests/README.md) and
[native composition checks](tests/deep-scan/README.md). Test scenarios that
change both owners do so explicitly; a separate fixture proves independence.

Concurrent settings notifications serialize their complete read/apply transactions,
so an older snapshot cannot overwrite a later edit. Input processing keeps its
existing locks; the additional adapter lock is used only for settings events.

## Installation

Add this module to your project's `config/west.yml` file.

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-matrix
      remote: amgskobo
      revision: main
```

For DYA runtime settings, also include [`zmk-feature-custom-settings`](https://github.com/cormoran/zmk-feature-custom-settings) in the
manifest and enable the integration in the central-side configuration:

```yaml
  remotes:
    - name: dya-source
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-custom-settings
      remote: dya-source
      revision: main
```

```conf
CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y
```

This option is not required for normal devicetree-only operation.

## Quick Start

### 1. DTS Include

Include the standard helper in your shield's `.overlay` or `.zmk.dts`:

```dts
#include <zmk-input-matrix/input_matrix.dtsi>
```

This helper defines the default `zip_matrix` input processor and `kscan_gesture`
KSCAN proxy nodes. You can skip it only if you define equivalent nodes yourself.

### 2. Configuration Example (3x3 Grid)

This example creates a **15-row x 3-column** matrix (5 gesture blocks x 3 zones):

**Note**: When `CONFIG_ZMK_POINTING` is enabled, enabling the compatible in DeviceTree automatically enables both `CONFIG_ZMK_INPUT_PROCESSOR_MATRIX` and `CONFIG_ZMK_KSCAN_INPUT_MATRIX` via Kconfig defaults.

```dts
/* Set kscan_gesture rows/columns to match your grid */
&kscan_gesture {
    rows = <15>;    /* 5 gestures * 3 grid rows */
    columns = <3>;
};

/* Configure the gesture grid */
&zip_matrix {
    rows = <3>;
    columns = <3>;
    flick-threshold = <50>;
    x = <1024>;
    y = <1024>;
    long-press-ms = <300>;
};

/* Add zip_matrix to your trackpad pipeline */
&trackpad_listener {
    input-processors = <&zip_matrix>;
};
```

#### Matrix Mapping

| Row Range | Gesture |
|:---------:|:-------:|
| 0 - 2 | Tap |
| 3 - 5 | Up |
| 6 - 8 | Down |
| 9 - 11 | Left |
| 12 - 14 | Right |

#### Gesture Semantics

- The start coordinate is latched on the first sync after `BTN_TOUCH` is pressed and both X/Y coordinates are initialized.
- A contact qualifies as a flick when its travel from the start point reaches `flick-threshold`. Travel is true distance, so the threshold describes a circle around the start point rather than a square: a diagonal stroke qualifies at the same length as a straight one.
- The contact keeps being followed after it qualifies, and the direction is read from the farthest point it reaches. The first sample past the threshold is the shortest vector of the whole stroke and the least reliable one to take a direction from.
- The long-press timer is canceled the moment a contact qualifies. Releasing touch reports the flick as a press+release pair.
- Long-press hold applies to the Tap block only. If no flick qualifies before `long-press-ms`, the tap cell is pressed and stays pressed until touch release, or until the layer changes.
- If no coordinates arrive, no gesture is emitted. If no flick qualifies and no hold is active, touch release reports Tap as a press+release pair.

#### Layer Changes

Which processors run is decided per event, from the layer active at that moment. A layer change therefore splits one contact between two chains: this processor stops being called, and the `BTN_TOUCH` release that would have ended the gesture is routed elsewhere.

Layer changes are watched directly so that cannot leave anything behind:

- A cell this instance has already pressed is released.
- A hold that is only pending is cancelled, so the timer cannot fire after the layer has changed and press a cell nothing can then release.
- The contact is closed out - the touch state and the buffered coordinates go the same way as the start point. A contact left marked active with its start cleared is the exact shape the sync path reads as a stroke beginning, so it would latch a new start and re-arm the hold that was just cancelled.
- Source button disposition and pending HID frames stay in the common final filter across these gesture resets.

The event fires on every layer change, including the ones that leave this processor in the chain - a layer key on the keyboard pressed while a finger is on the pad, say. A contact in progress is dropped either way: the pad stays inert until the finger is lifted and placed again.

Original source buttons pass unchanged after contact processing, including
release during a generation reset. The final owner applies its latched policy;
Matrix retains ownership of generated-key releases and hold cancellation.

### 3. Keymap Configuration

The `kscan_gesture` device acts as a standard 15x3 matrix. You can define keys for each gesture zone in your keymap:

```dts
/* In your .keymap file */
default_layer {
    bindings = <
        /* Row 0: Tap */
        &kp A &kp B &kp C
        /* Row 1: Tap */
        &kp D &kp E &kp F
        /* Row 2: Tap */
        &kp G &kp H &kp I

        /* Row 3: Up */
        &kp UP &kp UP &kp UP
        /* Row 4: Up */
        &kp UP &kp UP &kp UP
        /* Row 5: Up */
        &kp UP &kp UP &kp UP

        /* Row 6: Down */
        &kp DOWN &kp DOWN &kp DOWN
        /* Row 7: Down */
        &kp DOWN &kp DOWN &kp DOWN
        /* Row 8: Down */
        &kp DOWN &kp DOWN &kp DOWN

        /* Row 9: Left */
        &kp LEFT &kp LEFT &kp LEFT
        /* Row 10: Left */
        &kp LEFT &kp LEFT &kp LEFT
        /* Row 11: Left */
        &kp LEFT &kp LEFT &kp LEFT

        /* Row 12: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
        /* Row 13: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
        /* Row 14: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
    >;
};
```

### 4. Physical Layout (ZMK Studio)

To visualize the gesture grid with separated blocks in ZMK Studio, use [keymap-drawer](https://github.com/caksoylar/keymap-drawer) to generate the `keys` array:

```bash
# Generate keys for a 15x3 grid with gaps
python -m keymap_drawer.physical_layout_to_dt --cols-thumbs-notation "333+2 2+333"
```

Or use the [ZMK Physical Layout Converter](https://zmk-physical-layout-converter.streamlit.app/) web tool.

**Tip**: After generating, assign the layout to your kscan device:

```dts
&kscan_gesture {
    physical-layout = <&gesture_layout>;
};
```

See the [ZMK Physical Layouts](https://zmk.dev/docs/hardware-integration/physical-layouts) documentation for details.

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `rows` | int | Required | Grid rows |
| `columns` | int | Required | Grid columns |
| `x` | int | Required | Max X coordinate resolution |
| `y` | int | Required | Max Y coordinate resolution |
| `flick-threshold` | int | Required | Minimum travel from the start point to register a flick |
| `kscan` | phandle | Required | The `zmk,kscan-input-matrix` proxy that receives the gesture events |
| `long-press-ms` | int | 200 | Tap hold time (ms), 0 to disable |
| `diamond-tap` | bool | false | Report taps only, split into D-pad-style diamond zones on a 1x4 grid |

Limits are enforced at build time: `rows` must be 1-51, `columns` 1-255, `x`/`y` 1-65534, `flick-threshold` 1-65535, and `long-press-ms` 0-65535.

The linked `kscan_gesture` node must use matching `columns`, and rows equal to `zip_matrix.rows` times the number of gestures reported - **5 normally, 1 with `diamond-tap`**.

### Diamond Tap

`diamond-tap` splits the touch area along its two diagonals instead of using the rectangular grid, so a tap reports which cardinal zone the finger landed in. It requires `rows = <1>` and `columns = <4>`.

| Column | Tap zone |
| :---: | :--- |
| 0 | Up |
| 1 | Right |
| 2 | Down |
| 3 | Left |

Points on a diagonal boundary prefer the vertical axis, so the exact center maps to Down.

**Such an instance reports taps only.** A diamond takes its direction from where the finger comes to rest, which is the opposite of what a flick measures: on a small pad a stroke has to begin on the far side of the one it travels towards, leaving the two readings pointing opposite ways. The four flick rows are therefore never reached, and the kscan proxy behind a diamond instance needs one row per grid row instead of five:

```dts
&kscan_gesture {
    rows = <1>;     /* 1 gesture (Tap) * 1 grid row */
    columns = <4>;
};

&zip_matrix {
    rows = <1>;
    columns = <4>;
    x = <1024>;
    y = <1024>;
    diamond-tap;
};
```

`flick-threshold` is still required by the binding but has no effect on a diamond instance.

## 32-bit arithmetic and clock contract

Normalized geometry uses bounded `uint32_t` products. Squared travel keeps
one explicit carry bit alongside its 32-bit low word, retaining exact
farthest-point ordering up to the maximum supported coordinates.

Hold deadlines use modulo `uint32_t` uptime and explicit armed state,
including deadline zero after wrap. Pending deadlines assume elapsed
observations below 2^31 ms. The [test guide](tests/README.md) maps independent
wide references for large squared distances, direction/tie assertions, carry
ordering, wrapped/zero/unarmed deadlines, generated keys, resets and settings
races.

## Tests

```sh
bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh upstream
bash ./tests/run-integration-docker.sh dya
```

The runtime test lifts the driver's own functions into the stubbed harnesses in
`tests/runtime/`. Quick runs every host scenario under ASan/UBSan; full also
runs optimized and gcov variants with the complete line/branch gate. The
geometry harness checks grid edges, diamond zones, diagonal flick thresholds and
wide squared travel, then exhausts every coordinate of small rectangular and
diamond panels. The driver harness plays contacts through the event handler:
taps, flicks that follow the farthest point, long-press holds, touches and
settings writes landing while a hold is being pressed, paired key suppression,
per-listener streams and layer-change releases. Two more cover the virtual KSCAN
proxy and the settings helpers. Full-mode CI requires 100% line and branch coverage of
each lifted function: seven geometry functions, eighteen driver functions (including the layer
listener and layer cleanup), ten proxy functions and the settings bridge. The apply step that
turns stored settings into driver calls is lifted with its macro too, and the
runner fails if any function in the sources has no gate. Devicetree
instantiation itself is left to the firmware fixtures below.

Each variant builds a firmware fixture in which two input listeners share one
matrix node, and checks that the processor and the virtual KSCAN proxy are
enabled. `upstream` builds against ZMK `main` and confirms that the custom
settings stay off; `dya` builds against the DYA ZMK fork with custom settings on
and confirms that the `amgskobo__matrix` subsystem and all setting
keys are linked into the firmware. GitHub Actions runs both on every pull
request, on pushes to `main`, and weekly.

The integration variants run nine focused kernel scenarios and 46 Matrix
ownership/route/frame cases three times per binary. With the required pinned
IQS producer, both CI firmware jobs add 17 actual-producer cases (63 total).
DYA additionally checks two-node settings isolation through the actual registry.
They use actual delayed work, layer events and the KSCAN proxy recording callback;
see [the test guide](tests/README.md) for a prepared-workspace command and transport limits.

The standalone algorithm has no final-filter dependency. Composition tests require an explicit
`ZMK_TEST_SUPPRESS_ROOT`; no sibling path is assumed.

## License

MIT License. See [LICENSE](LICENSE) for details.
