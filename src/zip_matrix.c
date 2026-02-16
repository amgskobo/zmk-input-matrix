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

#ifndef ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX
#define ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(n, p, i) \
    { \
        .behavior_dev = DEVICE_DT_NAME(DT_PHANDLE_BY_IDX(n, p, i)), \
        .param1 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(n, p, i, param1), (0), (DT_PHA_BY_IDX(n, p, i, param1))), \
        .param2 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(n, p, i, param2), (0), (DT_PHA_BY_IDX(n, p, i, param2))), \
    }
#endif

struct grid_cell_config { struct zmk_behavior_binding bindings[5]; };

struct grid_processor_config {
    uint8_t rows, cols;
    uint16_t x_min, x_max, y_min, y_max, flick_threshold;
    bool suppress_pointer, suppress_key;
    const struct grid_cell_config *cells;
    size_t cell_count;
};

struct grid_processor_data {
    struct k_mutex lock;
    const struct grid_processor_config *config;
    uint16_t start_x, start_y, last_x, last_y;
    bool is_touching;
};

static uint8_t get_grid_cell(const struct grid_processor_config *cfg, uint16_t x, uint16_t y) {
    uint32_t dx = CLAMP(x, cfg->x_min, cfg->x_max) - cfg->x_min;
    uint32_t dy = CLAMP(y, cfg->y_min, cfg->y_max) - cfg->y_min;
    uint32_t w = cfg->x_max - cfg->x_min;
    uint32_t h = cfg->y_max - cfg->y_min;
    uint8_t col = (dx * cfg->cols) / w;
    uint8_t row = (dy * cfg->rows) / h;
    return MIN(row, cfg->rows - 1) * cfg->cols + MIN(col, cfg->cols - 1);
}

static void trigger_gesture(const struct device *dev) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = data->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->is_touching) {
        LOG_DBG("Release received but session not active. Resetting.");
        data->start_x = data->start_y = 0xFFFF;
        k_mutex_unlock(&data->lock);
        return;
    }
    uint16_t sx = data->start_x, sy = data->start_y, lx = data->last_x, ly = data->last_y;
    data->is_touching = false;
    data->start_x = data->start_y = 0xFFFF;
    k_mutex_unlock(&data->lock);

    int32_t dx = (int32_t)lx - sx, dy = (int32_t)ly - sy;
    uint32_t adx = (dx < 0 ? -dx : dx), ady = (dy < 0 ? -dy : dy);
    uint8_t dir = (adx < cfg->flick_threshold && ady < cfg->flick_threshold) ? 0 :
                  (ady > adx) ? (dy < 0 ? 1 : 2) : (dx < 0 ? 3 : 4);
    uint8_t cell = get_grid_cell(cfg, sx, sy);

    LOG_DBG("Gesture: Cell %u, Dir %u (delta X:%d Y:%d)", cell, dir, dx, dy);

    struct zmk_behavior_binding *binding = (struct zmk_behavior_binding *)&cfg->cells[cell].bindings[dir];
    if (binding->behavior_dev) {
        LOG_DBG("Triggering behavior: %s", binding->behavior_dev);
        struct zmk_behavior_binding_event event = { .position = INT32_MAX, .timestamp = k_uptime_get() };
        zmk_behavior_queue_add(&event, *binding, true, 0);
        zmk_behavior_queue_add(&event, *binding, false, 20);
    } else {
        LOG_DBG("No binding configured for Cell %u, Dir %u", cell, dir);
    }
}

static int input_processor_grid_handle_event(const struct device *dev, struct input_event *event,
                                             uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = data->config;

    if (event->type == INPUT_EV_ABS && (event->code == INPUT_ABS_X || event->code == INPUT_ABS_Y)) {
        k_mutex_lock(&data->lock, K_FOREVER);
        if (event->value == 0xFFFF) {
            LOG_DBG("Release sentinel (0xFFFF) received for axis %u", event->code);
            k_mutex_unlock(&data->lock);
            trigger_gesture(dev);
        } else {
            uint16_t *s = (event->code == INPUT_ABS_X) ? &data->start_x : &data->start_y;
            uint16_t *l = (event->code == INPUT_ABS_X) ? &data->last_x : &data->last_y;
            if (*s == 0xFFFF) *s = event->value;
            *l = event->value;
            LOG_DBG("Move: X:%u Y:%u", data->last_x, data->last_y);

            if (!data->is_touching && data->start_x != 0xFFFF && data->start_y != 0xFFFF) {
                LOG_DBG("Session active: Synchronized start at (%u, %u)", data->start_x, data->start_y);
                data->is_touching = true;
            }
            k_mutex_unlock(&data->lock);
        }
        return cfg->suppress_pointer ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
    }
    return (event->type == INPUT_EV_KEY && cfg->suppress_key) ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
}

static int input_processor_grid_init(const struct device *dev) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = dev->config;
    if (cfg->cell_count != (cfg->rows * cfg->cols)) return -EINVAL;
    if (cfg->x_max <= cfg->x_min || cfg->y_max <= cfg->y_min) return -EINVAL;
    if (cfg->rows == 0 || cfg->cols == 0) return -EINVAL;

    k_mutex_init(&data->lock);
    data->config = cfg;
    data->start_x = data->start_y = 0xFFFF;
    data->is_touching = false;
    LOG_INF("zip_matrix ready: %ux%u", cfg->rows, cfg->cols);
    return 0;
}

static const struct zmk_input_processor_driver_api grid_processor_driver_api = {
    .handle_event = input_processor_grid_handle_event,
};

#define GRID_CELL_CONFIG(node_id) { .bindings = { \
    ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 0), \
    ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 1), \
    ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 2), \
    ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 3), \
    ZMK_BEHAVIOR_BINDING_FROM_PHANDLE_ARRAY_BY_IDX(node_id, bindings, 4), \
} },

#define GRID_PROCESSOR_INST(n) \
    static struct grid_processor_data processor_grid_data_##n = {}; \
    static const struct grid_cell_config processor_grid_cells_##n[] = { DT_INST_FOREACH_CHILD(n, GRID_CELL_CONFIG) }; \
    static const struct grid_processor_config processor_grid_config_##n = { \
        .rows = DT_INST_PROP(n, rows), .cols = DT_INST_PROP(n, cols), \
        .x_min = DT_INST_PROP(n, x_min), .x_max = DT_INST_PROP(n, x_max), \
        .y_min = DT_INST_PROP(n, y_min), .y_max = DT_INST_PROP(n, y_max), \
        .flick_threshold = DT_INST_PROP(n, flick_threshold), \
        .suppress_pointer = DT_INST_PROP(n, suppress_pointer), .suppress_key = DT_INST_PROP(n, suppress_key), \
        .cells = processor_grid_cells_##n, .cell_count = ARRAY_SIZE(processor_grid_cells_##n), \
    }; \
    DEVICE_DT_INST_DEFINE(n, input_processor_grid_init, NULL, \
                           &processor_grid_data_##n, &processor_grid_config_##n, \
                           POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                           &grid_processor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GRID_PROCESSOR_INST)
