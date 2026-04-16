# ZMK Input Matrix (zip_matrix)

A ZMK Input Processor that converts trackpad absolute X/Y coordinates into a configurable gesture grid with long-press support. Gestures are reported as standard KSCAN matrix events for full ZMK Studio compatibility.

## Features

- **Dynamic Grid**: Configure any grid size (e.g., 1x1, 2x2, 3x3)
- **Block Layout**: Each gesture (Tap/Up/Down/Left/Right) gets a full block stacked vertically
- **SYN-Based Latching**: Coordinates are latched on `INPUT_SYN_REPORT` for stable start positions
- **Long-Press Support**: Configurable hold duration (set `0` to disable)
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

### 2. Configuration Example (3x3 Grid)

This example creates a **15 row × 3 column** matrix (5 gesture blocks × 3 zones):

**Note**: Enabling the compatible in DeviceTree automatically enables both `CONFIG_ZMK_INPUT_PROCESSOR_MATRIX` and `CONFIG_ZMK_KSCAN_INPUT_MATRIX` via Kconfig defaults.

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
| `long-press-ms` | int | 300 | Hold time (ms), 0 to disable |
| `suppress-abs` | bool | false | Consume ABS pointer events |
| `suppress-key` | bool | false | Consume KEY events (touch) |

## License

MIT License. See [LICENSE](LICENSE) for details.
