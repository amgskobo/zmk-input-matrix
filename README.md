# ZMK Input Matrix (zip_matrix)

A ZMK Input Processor that converts trackpad absolute X/Y coordinates into a configurable gesture grid with long-press support. Gestures are reported as standard KSCAN matrix events for full ZMK Studio compatibility.

## Features

- **Dynamic Grid**: Configure any grid size (e.g., 1x1, 2x2, 3x3)
- **Block Layout**: Each gesture (Tap/Up/Down/Left/Right) gets a full block stacked vertically
- **SYN-Based Latching**: Coordinates are latched on `INPUT_SYN_REPORT` for stable start positions
- **Long-Press Support**: Configurable tap-hold duration (set `0` to disable)
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
- A flick is latched when either X or Y displacement first reaches `flick-threshold`.
- Once a flick is latched, the long-press timer is canceled. Releasing touch reports the latched flick as a press+release pair.
- Long-press hold applies to the Tap block only. If no flick is latched before `long-press-ms`, the tap cell is pressed and stays pressed until touch release.
- If no coordinates arrive, no gesture is emitted. If no flick is latched and no hold is active, touch release reports Tap as a press+release pair.

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
| `flick-threshold` | int | Required | Minimum pixels for flick displacement |
| `long-press-ms` | int | 200 | Tap hold time (ms), 0 to disable |
| `suppress-abs` | bool | false | Consume all `INPUT_EV_ABS` events, not only X/Y |
| `suppress-touch` | bool | false | Consume only `INPUT_BTN_TOUCH` |
| `suppress-key` | bool | false | Consume all `INPUT_EV_KEY` events, including touchpad button gestures |
| `diamond-tap` | bool | false | Use D-pad-style diamond zones for Tap on a 1x4 grid |

Limits are enforced at build time: `rows` must be 1-51, `columns` 1-255, `x`/`y` 1-65534, `flick-threshold` 1-65535, and `long-press-ms` 0-65535. The linked `kscan_gesture` node must use `rows = 5 * zip_matrix.rows` and matching `columns`.

### Diamond Tap

When `diamond-tap;` is set, Tap gestures on a 1x4 grid use four diagonal zones:

| Column | Tap zone |
| :---: | :--- |
| 0 | Up |
| 1 | Right |
| 2 | Down |
| 3 | Left |

Flick gestures still use the regular rectangular 1x4 grid. `diamond-tap`
requires `rows = <1>` and `columns = <4>`.

## License

MIT License. See [LICENSE](LICENSE) for details.
