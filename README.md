# ZMK Input Matrix (zip_matrix)

This module implements a ZMK Input Processor that turns a trackpad (Absolute X/Y) into a configurable 4-way flick grid trigger.

## Features

- **Dynamic Grid**: Configure any size grid (e.g., 1x1, 2x2, 3x3, 4x3).
- **BTN_TOUCH Driven Triggering**: Responds immediately to standard `BTN_TOUCH` key events. Accurately captures the full touch-to-release lifecycle.
- **Precision Session Sync**: Waits for both X and Y axes to report initial coordinates *after* `BTN_TOUCH` goes high, preventing false triggers.
- **Flexible Event Filtering**: Independently suppress ABS (pointer) and KEY (button) events with `suppress-abs` and `suppress-key` properties.
- **Asynchronous Execution**: Uses ZMK's behavior queue for sequenced bindings (e.g., complex macros) without blocking input.
- **Optimized Calculation**: No movement-time overhead; coordinate-to-cell math is performed exactly once upon gesture completion.
- **5 Gestures per Cell**: Tap, Up, Down, Left, Right.
- **Concise Parameters**: Required `x`, `y`, and `threshold` for unambiguous configuration.

## Installation

Add this module to your `west.yml` or copy it to your ZMK config.

## Usage

### 1. include input_matrix.dtsi

```dts
#include <input_matrix.dtsi>
```

### 2. Configure Overlay

In your `*.overlay` (or keymap), define the processor and assign it to your input listener (`trackpad_listener` or similar).

#### Example: 4x3 Grid (Standard T9 Layout)

```dts
/* Define the processor */
&zip_matrix {
    rows = <4>;
    columns = <3>;
    threshold = <50>;
    x = <4095>;
    y = <4095>;

    /* 
     * Standard 12-Key T9 Layout (4 Rows x 3 Columns)
     * [00] [01] [02]
     * [03] [04] [05]
     * [06] [07] [08]
     * [09] [10] [11]
     */

    cell_00 { bindings = <&kp N1 &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_01 { bindings = <&kp A  &kp C  &kp N2   &kp B    &kp A>; };    /* Tap=A, Up=C, Down=N2, Left=B, Right=A */
    cell_02 { bindings = <&kp D  &kp F  &kp N3   &kp E    &kp D>; };
    
    /* ... and so on */
};

/* Assign to Listener */
&trackpad_listener {
    input-processors = <&zip_matrix>;
};
```

## Configuration Reference

### Processor Properties

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `rows` | `int` | **Required** | Number of rows in the grid. |
| `columns` | `int` | **Required** | Number of columns in the grid. |
| `threshold` | `int` | **Required** | Min pixels for a flick vs tap. |
| `x` | `int` | **Required** | Maximum X coordinate (range is 0 to x). |
| `y` | `int` | **Required** | Maximum Y coordinate (range is 0 to y). |
| `suppress-abs` | `bool` | `false` | If `true`, stops ABS event propagation (disables cursor movement). |
| `suppress-key` | `bool` | `false` | If `true`, stops KEY event propagation (disables physical clicks). |

### Child Nodes & Grid Mapping

The driver maps child nodes to grid cells in **Row-Major Order** (Left-to-Right, Top-to-Bottom).

#### Mapping Logic

1. **Ordering**: The driver processes child nodes in **Definition Order** (the order they appear in your Devicetree file).
2. **Indexing**: Nodes are assigned to grid cells starting from index 0.
    - `Index = (Row * Total_Cols) + Col`

**Important**: Because grid cells are assigned in the order they are defined, it is recommended to name them sequentially (e.g., `cell_00`, `cell_01`... `cell_11`) to avoid confusion.

#### 5-Way Binding Format

Each child node must have a `bindings` array with exactly 5 entries in this specific order:

1. **Tap** (Center / No Flick)
2. **Up** (Flick Up)
3. **Down** (Flick Down)
4. **Left** (Flick Left)
5. **Right** (Flick Right)

## Event Flow and Transparency

`zip_matrix` can either "consume" events or let them "pass through" (transparency) to the next processor in the chain.

| Property | Behavior when `true` (Suppressed) | Behavior when `false` (Transparent) |
| :--- | :--- | :--- |
| `suppress-abs` | Consumes ABS events. Cursor will **not** move. | Passes ABS events through. Cursor **will** move. |
| `suppress-key` | Consumes KEY events. BTN_TOUCH etc. will **not** click. | Passes KEY events through. BTN_TOUCH etc. **will** click. |

> [!TIP]
> To use a trackpad **only** as a macro grid, set both `suppress-abs` and `suppress-key` to `true`.
> To use it as a mouse **with** gesture capabilities, set them to `false`, and place `zip_matrix` **before** any ABS-to-REL conversion in your `input-processors` list.
