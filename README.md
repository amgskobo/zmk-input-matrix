# ZMK Input Matrix (zip_matrix)

A ZMK Input Processor that converts trackpad absolute X/Y coordinates into a configurable gesture grid with long-press support. Gestures are reported as standard KSCAN matrix events for full ZMK Studio compatibility.

## Features

- **Dynamic Grid**: Configure any grid size (e.g., 1x1, 2x2, 3x3)
- **Block Layout**: Each gesture (Tap/Up/Down/Left/Right) gets a full block stacked vertically
- **SYN-Based Latching**: Coordinates are latched on `INPUT_SYN_REPORT` for stable start positions
- **Long-Press Support**: Configurable tap-hold duration (set `0` to disable)
- **Layer-Change Safe**: A held cell is released, and a pending hold cancelled, when the layer changes out from under the contact
- **Split Keyboard Ready**: Optimized for central-side processing
- **Thread-Safe**: Uses spinlocks to prevent race conditions

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
- The record of which suppressed presses are still outstanding is dropped, so a later unrelated release cannot be swallowed in their place.

`suppress-key` never drops a release whose press was not suppressed here. Passing a release through is always safe - the press it belongs to already reached the host - while dropping one would leave that button held down with nothing left to release it.

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

See the [ZMK Physical Layouts](/docs/development/hardware-integration/physical-layouts) documentation for details.

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
| `suppress-abs` | bool | false | Consume all `INPUT_EV_ABS` events, not only X/Y |
| `suppress-touch` | bool | false | Consume only `INPUT_BTN_TOUCH` |
| `suppress-key` | bool | false | Consume all `INPUT_EV_KEY` events, including touchpad button gestures |
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

## License

MIT License. See [LICENSE](LICENSE) for details.
