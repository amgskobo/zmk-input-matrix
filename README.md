# ZMK Input Processor - Grid Matrix

A ZMK input processor module that converts trackpad absolute coordinates into a **3×3 grid layer controller**.

## Features

- **3×3 Grid Mapping**: Divides the 0-1024 coordinate space into 9 cells
- **Layer Activation**: Each cell maps to a ZMK layer (6-14)
- **Touch Detection**: Detects touch via coordinate updates (no EV_KEY needed)
- **Auto-Deactivation**: 80ms watchdog timer simulates finger lift
- **Standard Input Processor**: Follows ZMK input processor API

## Grid Layout

```
Layer 6  │ Layer 7  │ Layer 8     (0-341 Y)
─────────┼──────────┼─────────    
Layer 9  │ Layer 10 │ Layer 11    (341-682 Y)
─────────┼──────────┼─────────
Layer 12 │ Layer 13 │ Layer 14    (682-1024 Y)
(0-341 X) (341-682 X) (682-1024 X)
```

## Installation

1. **Add to zmk-config**
   ```bash
   cd ~/zmk-config
   git clone https://github.com/amgskobo/zmk-input-matrix.git zmk-input-matrix
   ```

2. **Update west.yml**
   ```yaml
   projects:
     - name: zmk-input-matrix
       path: zmk-input-matrix
       url-base: file://
       revision: main
   ```

3. **Run `west update`**

4. **Enable in config** (`config/your_board.conf`)
   ```kconfig
   CONFIG_ZMK_INPUT_PROCESSOR_GRID_MATRIX=y
   ```

5. **Add to devicetree** (shield/board overlay)
   ```dts
   &zip_input_processor_grid_matrix {
       status = "okay";
   };
   ```

6. **Define layers 6-14** in your keymap

## Configuration

### Build Options
```kconfig
CONFIG_ZMK_INPUT_PROCESSOR_GRID_MATRIX=y  # Enable module
```

### Hardcoded Parameters
Edit `drivers/input/input_processor_grid_matrix.c`:
- `TRACKPAD_MIN/MAX`: Coordinate range (0-1024)
- `GRID_COLS/ROWS`: Grid dimensions (3×3)
- `GRID_BASE_LAYER`: Starting layer (6)
- `WATCHDOG_TIMEOUT_MS`: Touch-up timeout (80ms)

## How It Works

1. Trackpad sends ABS_X/ABS_Y events
2. Module calculates grid cell (0-8)
3. Determines target layer (6-14)
4. Activates layer
5. Resets 80ms watchdog
6. After 80ms with no updates → layer deactivates

## File Structure

```
zmk-input-matrix/
├── drivers/input/
│   ├── input_processor_grid_matrix.c
│   ├── CMakeLists.txt
│   └── Kconfig
├── dts/
│   ├── behaviors/
│   │   └── input_processor_grid_matrix.dtsi
│   └── bindings/
│       └── input/processors/zmk,input-processor-grid-matrix.yaml
├── config/
│   ├── example.conf
│   ├── example.keymap
│   ├── example.overlay
│   └── west.yml
├── zephyr/
│   └── module.yml
├── CMakeLists.txt
├── Kconfig
└── README.md
```

## Documentation

- **[INTEGRATION.md](INTEGRATION.md)** - Integration & troubleshooting
- **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** - Quick reference guide
- **[config/example.conf](config/example.conf)** - Example configuration
- **[config/example.overlay](config/example.overlay)** - Example devicetree
- **[config/example.keymap](config/example.keymap)** - Example keymap

## License

MIT License
