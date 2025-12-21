/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_matrix

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>
#include <zmk/behavior_queue.h>

LOG_MODULE_REGISTER(zmk_input_processor_matrix, CONFIG_ZMK_LOG_LEVEL);

/* * MANUAL MACRO DEFINITION (SHIM)
 * This replaces the missing ZMK macro. It extracts behavior + params from the Devicetree.
 * We use DEVICE_DT_NAME to be compatible with recent ZMK versions.
 */
#ifndef ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX
#define ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(n, p, i) \
    { \
        .behavior_dev = DEVICE_DT_NAME(DT_PHANDLE_BY_IDX(n, p, i)), \
        .param1 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(n, p, i, param1), (0), (DT_PHA_BY_IDX(n, p, i, param1))), \
        .param2 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(n, p, i, param2), (0), (DT_PHA_BY_IDX(n, p, i, param2))), \
    }
#endif
/* 
 * Each cell has exactly 5 bindings:
 * 0: Center (Tap)
 * 1: North
 * 2: South
 * 3: West
 * 4: East
 */
struct grid_cell_config {
    struct zmk_behavior_binding bindings[5];
};

struct grid_processor_config {
    uint8_t rows;
    uint8_t cols;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    uint16_t flick_threshold;
    uint16_t timeout_ms;
    bool suppress_input;
    const struct grid_cell_config *cells;
    size_t cell_count;
};

struct grid_processor_data {
    struct k_work_delayable watchdog;
    struct k_mutex lock;
    const struct device *dev;
    const struct grid_processor_config *config;
    uint16_t last_x;
    uint16_t last_y;
    uint16_t start_x;
    uint16_t start_y;
    uint32_t cell_w_inv; /* Fixed-point reciprocal (Q16) */
    uint32_t cell_h_inv; /* Fixed-point reciprocal (Q16) */
    bool is_touching;
};

static inline uint8_t get_grid_cell(const struct grid_processor_config *config, 
                                    struct grid_processor_data *data, 
                                    uint16_t x, uint16_t y) {
    uint16_t dx = CLAMP(x, config->x_min, config->x_max) - config->x_min;
    uint16_t dy = CLAMP(y, config->y_min, config->y_max) - config->y_min;

    /* Use 64-bit intermediate for overflow-proof 16:16 fixed-point math */
    uint8_t col = (uint8_t)(((uint64_t)dx * data->cell_w_inv) >> 16);
    uint8_t row = (uint8_t)(((uint64_t)dy * data->cell_h_inv) >> 16);

    if (col >= config->cols) col = config->cols - 1;
    if (row >= config->rows) row = config->rows - 1;

    return row * config->cols + col;
}

static inline uint8_t get_direction_4way(int32_t dx, int32_t dy, uint16_t threshold) {
    uint32_t abs_dx = (dx < 0) ? -dx : dx;
    uint32_t abs_dy = (dy < 0) ? -dy : dy;

    if (abs_dx < (uint32_t)threshold && abs_dy < (uint32_t)threshold) {
        return 0; /* Tap/Center */
    }

    if (abs_dy > abs_dx) {
        return (dy < 0) ? 1 : 2; /* North : South */
    } else {
        return (dx < 0) ? 3 : 4; /* West : East */
    }
}

static void trigger_gesture(const struct device *dev) {
    struct grid_processor_data *data = (struct grid_processor_data *)dev->data;
    const struct grid_processor_config *config = data->config;

    if (!data->is_touching) {
        return;
    }
    data->is_touching = false;

    uint8_t cell_idx = get_grid_cell(config, data, data->start_x, data->start_y);
    if (cell_idx < config->cell_count) {
        /* Use 32-bit signed math to prevent 16-bit overflow during subtraction */
        int32_t dx = (int32_t)data->last_x - (int32_t)data->start_x;
        int32_t dy = (int32_t)data->last_y - (int32_t)data->start_y;
        uint8_t dir = get_direction_4way(dx, dy, config->flick_threshold);
        
        LOG_DBG("Gesture: Cell %u, Dir %u (delta %d,%d)", cell_idx, dir, dx, dy);

        struct zmk_behavior_binding *binding = (struct zmk_behavior_binding *)&config->cells[cell_idx].bindings[dir];
        if (binding->behavior_dev) {
            struct zmk_behavior_binding_event event = {
                .position = INT32_MAX,
                .timestamp = k_uptime_get()
            };
            zmk_behavior_queue_add(&event, *binding, true, 0);
            zmk_behavior_queue_add(&event, *binding, false, 30);
        }
    }
}

static void watchdog_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct grid_processor_data *data = CONTAINER_OF(dwork, struct grid_processor_data, watchdog);

    /* Use a reasonable timeout to ensure we don't drop the gesture under high CPU load */
    if (k_mutex_lock(&data->lock, K_MSEC(50)) == 0) {
        trigger_gesture(data->dev);
        k_mutex_unlock(&data->lock);
    } else {
        LOG_WRN("Watchdog lock timeout; rescheduling");
        k_work_reschedule(&data->watchdog, K_MSEC(10));
    }
}

static int input_processor_grid_handle_event(const struct device *dev,
                                              struct input_event *event,
                                              uint32_t param1,
                                              uint32_t param2,
                                              struct zmk_input_processor_state *state) {
    struct grid_processor_data *data = (struct grid_processor_data *)dev->data;

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (event->code == INPUT_ABS_X) {
        data->last_x = event->value;
    } else if (event->code == INPUT_ABS_Y) {
        data->last_y = event->value;
    } else {
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!data->is_touching) {
        data->is_touching = true;
        data->start_x = data->last_x;
        data->start_y = data->last_y;
    }

    k_work_reschedule(&data->watchdog, K_MSEC(data->config->timeout_ms));
    bool suppress = data->config->suppress_input;
    
    k_mutex_unlock(&data->lock);
    return suppress ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
}

static int input_processor_grid_init(const struct device *dev) {
    struct grid_processor_data *data = (struct grid_processor_data *)dev->data;
    const struct grid_processor_config *config = (const struct grid_processor_config *)dev->config;

    if (config->cell_count != (config->rows * config->cols)) {
        LOG_ERR("[%s] Mismatch: Rows*Cols (%u) != Child Nodes (%u)", 
                dev->name, config->rows * config->cols, (uint32_t)config->cell_count);
        return -EINVAL; 
    }

    if (config->x_max <= config->x_min || config->y_max <= config->y_min) {
        LOG_ERR("[%s] Invalid range: X[%u-%u] Y[%u-%u]", 
                dev->name, config->x_min, config->x_max, config->y_min, config->y_max);
        return -EINVAL;
    }

    k_mutex_init(&data->lock);
    data->dev = dev;
    data->config = config;
    data->is_touching = false;

    uint32_t w = config->x_max - config->x_min;
    uint32_t h = config->y_max - config->y_min;

    if (config->cols > 0) {
        data->cell_w_inv = ((uint64_t)config->cols << 16) / w;
    }
    if (config->rows > 0) {
        data->cell_h_inv = ((uint64_t)config->rows << 16) / h;
    }

    data->last_x = (config->x_min + config->x_max) / 2;
    data->last_y = (config->y_min + config->y_max) / 2;

    k_work_init_delayable(&data->watchdog, watchdog_callback);

    LOG_INF("zip_matrix[%s] %ux%u ready", dev->name, config->rows, config->cols);
    return 0;
}

static const struct zmk_input_processor_driver_api grid_processor_driver_api = {
    .handle_event = input_processor_grid_handle_event,
};

#define GRID_CELL_CONFIG(node_id)                                             \
    {                                                                         \
        .bindings = {                                                         \
            ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 0), \
            ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 1), \
            ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 2), \
            ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 3), \
            ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 4), \
        }                                                                     \
    },

#define GRID_PROCESSOR_INST(n)                                                                \
    static struct grid_processor_data processor_grid_data_##n = {};                           \
                                                                                               \
    static const struct grid_cell_config processor_grid_cells_##n[] = {                       \
        DT_INST_FOREACH_CHILD(n, GRID_CELL_CONFIG)                                            \
    };                                                                                        \
                                                                                               \
    static const struct grid_processor_config processor_grid_config_##n = {                   \
        .rows = DT_INST_PROP(n, rows),                                                        \
        .cols = DT_INST_PROP(n, cols),                                                        \
        .x_min = DT_INST_PROP(n, x_min),                                                      \
        .x_max = DT_INST_PROP(n, x_max),                                                      \
        .y_min = DT_INST_PROP(n, y_min),                                                      \
        .y_max = DT_INST_PROP(n, y_max),                                                      \
        .flick_threshold = DT_INST_PROP(n, flick_threshold),                                  \
        .timeout_ms = DT_INST_PROP(n, timeout_ms),                                            \
        .suppress_input = DT_INST_PROP(n, suppress_input),                                    \
        .cells = processor_grid_cells_##n,                                                    \
        .cell_count = ARRAY_SIZE(processor_grid_cells_##n),                                   \
    };                                                                                        \
    DEVICE_DT_INST_DEFINE(n, input_processor_grid_init, NULL,                                \
                           &processor_grid_data_##n, &processor_grid_config_##n,               \
                           POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                  \
                           &grid_processor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GRID_PROCESSOR_INST)
