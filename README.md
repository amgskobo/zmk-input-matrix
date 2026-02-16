# ZMK Input Matrix (zip_matrix)

This module implements a ZMK Input Processor that turns a trackpad (Absolute X/Y) into a configurable 4-way flick grid trigger.

## Features

- **Dynamic Grid**: Configure any size grid (e.g., 1x1, 2x2, 3x3, 4x3).
- **Robust Triggering**: Uses a precision silence watchdog to detect gesture completion, ensuring compatibility with all trackpad drivers.
- **Flexible Event Filtering**: Independently suppress ABS (pointer) and KEY (button) events with `suppress-pointer` and `suppress-key` properties.
- **Asynchronous Execution**: Uses ZMK's behavior queue for sequenced bindings (e.g., complex macros) without blocking input.
- **Lightweight Math**: High-performance Q16 fixed-point math and max-axis comparison for minimal MCU overhead.
- **5 Gestures per Cell**: Center (Tap), North, South, West, East.
- **Simple Tap Logic**: Triggers a Center (Tap) event if the signal stops and `timeout-ms` elapses without any flick movement.

## Installation

Add this module to your `west.yml` or copy it to your ZMK config.

## Usage

### 1. include input_matrix.dtsi

```dts
#include <input_matrix.dtsi>
```

### 2. Configure Overlay

In your `*.overlay` (or keymap), define the processor and assign it to your input listener (`trackpad_listener` or similar).

**Note**: You do not need to enable a Kconfig option manually. The module is automatically enabled when you use the `zmk,input-processor-matrix` compatible string in your overlay.

**Example: 4x3 Grid (Standard T9 Layout)**

```dts
/* Define the processor */
&zip_matrix {
    rows = <4>; cols = <3>;
    timeout-ms = <100>;
    flick-threshold = <50>;

    /* 
     * Standard 12-Key T9 Layout (4 Rows x 3 Columns)
     * [1] [2] [3]
     * [4] [5] [6]
     * [7] [8] [9]
     * [*] [0] [#]
     */

    /* Row 0: 1, 2(ABC), 3(DEF) */
    cell_00 { bindings = <&kp N1 &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_01 { bindings = <&kp A  &kp C  &kp N2   &kp B    &kp A>; };    /* Tap=A, Left=B, Up=C */
    cell_02 { bindings = <&kp D  &kp F  &kp N3   &kp E    &kp D>; };    /* Tap=D, Left=E, Up=F */

    /* Row 1: 4(GHI), 5(JKL), 6(MNO) */
    cell_03 { bindings = <&kp G  &kp I  &kp N4   &kp H    &kp G>; };
    cell_04 { bindings = <&kp J  &kp L  &kp N5   &kp K    &kp J>; };
    cell_05 { bindings = <&kp M  &kp O  &kp N6   &kp N    &kp M>; };

    /* Row 2: 7(PQRS), 8(TUV), 9(WXYZ) */
    cell_06 { bindings = <&kp P  &kp R  &kp S    &kp Q    &kp N7>; };
    cell_07 { bindings = <&kp T  &kp V  &kp N8   &kp U    &kp T>; };
    cell_08 { bindings = <&kp W  &kp Y  &kp Z    &kp X    &kp N9>; };

    /* Row 3: *, 0, # */
    cell_09 { bindings = <&kp STAR  &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_10 { bindings = <&kp N0    &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_11 { bindings = <&kp HASH  &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
};

/* Assign to Listener */
&trackball_listener {
    input-processors = <&zip_matrix>;
};
```

## Configuration Reference

### Processor Properties

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `rows` | `int` | **Required** | Number of rows. |
| `cols` | `int` | **Required** | Number of columns. |
| `timeout-ms` | `int` | `100` | Time (ms) after signal loss to confirm a Tap gesture. |
| `cooldown-ms` | `int` | `100` | Cooldown period (ms) after gesture execution before accepting new input. |
| `flick-threshold` | `int` | `300` | Min pixels for a flick vs tap. |
| `suppress-pointer` | `bool` | `false` | If `true`, stops ABS event propagation (disables cursor movement). |
| `suppress-key` | `bool` | `false` | If `true`, stops KEY event propagation (disables BTN_TOUCH clicks). |
| `x-min`/`x-max` | `int` | `0`/`1024` | Input range. |
| `y-min`/`y-max` | `int` | `0`/`1024` | Input range. |

### Child Nodes & Grid Mapping

The driver maps child nodes to grid cells in **Row-Major Order** (Left-to-Right, Top-to-Bottom).

#### Mapping Logic

1. **Sorting**: The driver processes child nodes in **Alphabetical Order** of their node names.
2. **Indexing**: Nodes are assigned to grid cells starting from index 0.
    - `Index = (Row * Total_Cols) + Col`

**Important**: Because of alphabetical sorting, use leading zeros if you have 10 or more cells (e.g., `cell_01`...`cell_10`) to keep the correct order.

#### Example: 4x3 Grid (12 Cells)

`rows = <4>; cols = <3>;`

| | Column 0 (Left) | Column 1 (Center) | Column 2 (Right) |
| :--- | :--- | :--- | :--- |
| **Row 0 (Top)** | `cell_00` (Idx 0) | `cell_01` (Idx 1) | `cell_02` (Idx 2) |
| **Row 1 (Mid)** | `cell_03` (Idx 3) | `cell_04` (Idx 4) | `cell_05` (Idx 5) |
| **Row 2 (Bot)** | `cell_06` (Idx 6) | `cell_07` (Idx 7) | `cell_08` (Idx 8) |
| **Row 3 (Low)** | `cell_09` (Idx 9) | `cell_10` (Idx 10) | `cell_11` (Idx 11) |

#### Binding Format

Each child node must have a `bindings` array with exactly 5 entries in this specific order:

1. **Center** (Tap / No Flick)
2. **North** (Flick Up)
3. **South** (Flick Down)
4. **West** (Flick Left)
5. **East** (Flick Right)

## Event Transparency and Coordinate Systems

### Absolute (ABS) vs Relative (REL) Coordinates

`zip_matrix` is designed to work with **Absolute Coordinates (ABS)**.

- **Hardware Level**: Most touchpads and touch sensors report absolute positions (e.g., X: 0-1024, Y: 0-1024).
- **zip_matrix**: Processes these ABS events to determine which grid cell is being touched and detects "flicks" (large delta in ABS coordinates).
- **Standard Mouse**: ZMK's standard mouse reporting typically converts ABS events into **Relative (REL)** deltas (movement) using a converter or another input processor.

### Event Flow & Transparency

Input processors in ZMK form a chain. `zip_matrix` can either "consume" events or let them "pass through" (transparency) to the next processor in the chain.

| Property | Behavior when `true` (Suppressed) | Behavior when `false` (Transparent) |
| :--- | :--- | :--- |
| `suppress-pointer` | Consumes ABS events. Cursor will **not** move. | Passes ABS events through. Cursor **will** move. |
| `suppress-key` | Consumes KEY events (clicks). BTN_TOUCH etc. will **not** trigger clicks. | Passes KEY events through. BTN_TOUCH etc. **will** trigger clicks. |

> [!TIP]
> To use a trackpad **only** as a macro grid (like a T9 keypad), set both `suppress-pointer` and `suppress-key` to `true`.
> To use it as a mouse **with** gesture capabilities (e.g., flicking at edges), set them to `false`, but ensure `zip_matrix` is positioned **before** any ABS-to-REL conversion in your `input-processors` list.

```
