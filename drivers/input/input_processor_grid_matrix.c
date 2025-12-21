/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_grid_matrix

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(zmk_input_processor_grid_matrix, CONFIG_ZMK_LOG_LEVEL);

/* Grid configuration: 3x3 grid mapping to layers 6-14 */
#define GRID_COLS 3
#define GRID_ROWS 3
#define GRID_BASE_LAYER 6
#define TRACKPAD_MIN 0
#define TRACKPAD_MAX 1024
#define WATCHDOG_TIMEOUT_MS 80

/* Safety macro */
#define CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

struct grid_processor_config {
    uint8_t time_between_reports;
};

struct grid_processor_data {
    struct k_work_delayable watchdog;
    uint16_t last_x;
    uint16_t last_y;
    uint8_t active_layer;
    bool layer_active;
    const struct device *dev;
};

/**
 * Watchdog callback: deactivates layer when no coordinates received for timeout.
 */
static void watchdog_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct grid_processor_data *data =
        CONTAINER_OF(dwork, struct grid_processor_data, watchdog);

    if (data->layer_active) {
        LOG_INF("Watchdog timeout: deactivating layer %u", data->active_layer);
        zmk_keymap_layer_deactivate(data->active_layer);
        data->layer_active = false;
    }
}

/**
 * Calculate grid cell index from trackpad coordinates.
 * 
 * @param x: X coordinate (0-1024)
 * @param y: Y coordinate (0-1024)
 * @return: Layer index relative to GRID_BASE_LAYER (0-8 for 3x3 grid)
 */
static uint8_t get_grid_cell(uint16_t x, uint16_t y) {
    /* Clamp coordinates to valid range */
    x = CLAMP(x, TRACKPAD_MIN, TRACKPAD_MAX);
    y = CLAMP(y, TRACKPAD_MIN, TRACKPAD_MAX);

    /* Calculate grid dimensions */
    uint32_t cell_width = (TRACKPAD_MAX - TRACKPAD_MIN) / GRID_COLS;
    uint32_t cell_height = (TRACKPAD_MAX - TRACKPAD_MIN) / GRID_ROWS;

    /* Safety check */
    if (cell_width == 0 || cell_height == 0) {
        LOG_WRN("Invalid grid dimensions");
        return 0;
    }

    /* Calculate grid indices */
    uint8_t col = (x - TRACKPAD_MIN) / cell_width;
    uint8_t row = (y - TRACKPAD_MIN) / cell_height;

    /* Clamp to valid bounds */
    col = CLAMP(col, 0, GRID_COLS - 1);
    row = CLAMP(row, 0, GRID_ROWS - 1);

    /* Convert row,col to linear cell index (row-major) */
    uint8_t cell_idx = row * GRID_COLS + col;

    LOG_DBG("Trackpad (%u, %u) -> Grid [%u, %u] -> Cell %u -> Layer %u",
            x, y, col, row, cell_idx, GRID_BASE_LAYER + cell_idx);

    return cell_idx;
}

/**
 * Main input event handler - implements zmk_input_processor_driver_api.
 */
static int input_processor_grid_handle_event(const struct device *dev,
                                              struct input_event *event,
                                              uint32_t param1,
                                              uint32_t param2,
                                              struct zmk_input_processor_state *state) {
    struct grid_processor_data *data = (struct grid_processor_data *)dev->data;

    /* Only process absolute coordinate events */
    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE; /* Pass through */
    }

    /* Update coordinates based on event */
    if (event->code == INPUT_ABS_X) {
        data->last_x = event->value;
        LOG_DBG("Updated X: %u", data->last_x);
    } else if (event->code == INPUT_ABS_Y) {
        data->last_y = event->value;
        LOG_DBG("Updated Y: %u", data->last_y);
    } else {
        return ZMK_INPUT_PROC_CONTINUE; /* Pass through */
    }

    /* Calculate grid cell for current position */
    uint8_t grid_cell = get_grid_cell(data->last_x, data->last_y);
    uint8_t target_layer = GRID_BASE_LAYER + grid_cell;

    /* Deactivate previous layer if different */
    if (data->layer_active && data->active_layer != target_layer) {
        LOG_INF("Layer transition: %u -> %u", data->active_layer, target_layer);
        zmk_keymap_layer_deactivate(data->active_layer);
    }

    /* Activate new layer */
    if (!data->layer_active || data->active_layer != target_layer) {
        LOG_INF("Activating layer %u (cell %u)", target_layer, grid_cell);
        zmk_keymap_layer_activate(target_layer);
        data->active_layer = target_layer;
        data->layer_active = true;
    }

    /* Restart watchdog timer */
    k_work_reschedule(&data->watchdog, K_MSEC(WATCHDOG_TIMEOUT_MS));

    return ZMK_INPUT_PROC_CONTINUE; /* Pass through */
}

static int input_processor_grid_init(const struct device *dev) {
    struct grid_processor_data *data = (struct grid_processor_data *)dev->data;

    LOG_INF("Initializing ZMK Input Processor Grid Matrix");
    LOG_INF("Grid configuration: 3x3 matrix (Layers %u-%u)",
            GRID_BASE_LAYER, GRID_BASE_LAYER + 8);

    data->dev = dev;
    data->layer_active = false;
    data->active_layer = GRID_BASE_LAYER;
    data->last_x = 512; /* Default to center */
    data->last_y = 512;

    /* Initialize watchdog timer */
    k_work_init_delayable(&data->watchdog, watchdog_callback);

    LOG_INF("Input processor initialized and ready");
    return 0;
}

static const struct zmk_input_processor_driver_api grid_processor_driver_api = {
    .handle_event = input_processor_grid_handle_event,
};

#define GRID_PROCESSOR_INST(n)                                                                \
    static struct grid_processor_data processor_grid_data_##n = {                             \
        .layer_active = false,                                                                \
        .active_layer = GRID_BASE_LAYER,                                                      \
        .last_x = 512,                                                                        \
        .last_y = 512,                                                                        \
    };                                                                                        \
    static const struct grid_processor_config processor_grid_config_##n = {                   \
        .time_between_reports = WATCHDOG_TIMEOUT_MS,                                          \
    };                                                                                        \
    DEVICE_DT_INST_DEFINE(n, input_processor_grid_init, NULL,                                \
                          &processor_grid_data_##n, &processor_grid_config_##n,               \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                  \
                          &grid_processor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GRID_PROCESSOR_INST)
