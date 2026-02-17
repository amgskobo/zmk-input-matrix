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
    uint8_t rows, columns;
    uint16_t x, y, threshold;
    bool suppress_abs, suppress_key;
    const struct grid_cell_config *cells;
    size_t cell_count;
};

struct grid_processor_data {
    struct k_mutex lock;
    const struct grid_processor_config *config;
    uint16_t start_x, start_y, last_x, last_y;
    bool session_active;  /* Synchronized X and Y received within current touch window */
    bool is_btn_touch;    /* Physical BTN_TOUCH state (ON/OFF) */
};

static uint8_t get_grid_cell(const struct grid_processor_config *cfg, uint16_t x, uint16_t y) {
    uint32_t pos_x = CLAMP(x, 0, cfg->x);
    uint32_t pos_y = CLAMP(y, 0, cfg->y);
    
    /* Ensure no division by zero even if config is somehow zero */
    uint32_t max_x = MAX(1, cfg->x);
    uint32_t max_y = MAX(1, cfg->y);
    
    uint8_t col = MIN(cfg->columns - 1, (uint8_t)((pos_x * cfg->columns) / max_x));
    uint8_t row = MIN(cfg->rows - 1, (uint8_t)((pos_y * cfg->rows) / max_y));
    
    return (row * cfg->columns) + col;
}

static void trigger_gesture(const struct device *dev) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = data->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    bool active = data->session_active;
    uint16_t start_x = data->start_x, start_y = data->start_y;
    uint16_t end_x = data->last_x, end_y = data->last_y;
    data->session_active = false;
    k_mutex_unlock(&data->lock);

    if (!active) {
        LOG_DBG("Release ignored (session never activated)");
        return;
    }

    int32_t delta_x = (int32_t)end_x - (int32_t)start_x;
    int32_t delta_y = (int32_t)end_y - (int32_t)start_y;
    uint32_t abs_delta_x = (uint32_t)(delta_x < 0 ? -delta_x : delta_x);
    uint32_t abs_delta_y = (uint32_t)(delta_y < 0 ? -delta_y : delta_y);

    /* 0:Tap, 1:Up, 2:Down, 3:Left, 4:Right */
    uint8_t gesture = (abs_delta_x < cfg->threshold && abs_delta_y < cfg->threshold) ? 0 :
                      (abs_delta_y > abs_delta_x) ? (delta_y < 0 ? 1 : 2) : (delta_x < 0 ? 3 : 4);
    uint8_t cell = get_grid_cell(cfg, start_x, start_y);

    LOG_DBG("Gesture: Cell %u, Dir %u (dx=%d, dy=%d)", cell, gesture, delta_x, delta_y);

    const struct zmk_behavior_binding *binding = &cfg->cells[cell].bindings[gesture];
    if (binding->behavior_dev) {
        struct zmk_behavior_binding_event event = { .position = INT32_MAX, .timestamp = k_uptime_get() };
        zmk_behavior_queue_add(&event, *binding, true, 0);
        zmk_behavior_queue_add(&event, *binding, false, 20);
    }
}

static int input_processor_grid_handle_event(const struct device *dev, struct input_event *event,
                                             uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = data->config;

    switch (event->type) {
    case INPUT_EV_ABS:
        if (event->code != INPUT_ABS_X && event->code != INPUT_ABS_Y) break;
        
        k_mutex_lock(&data->lock, K_FOREVER);
        if (data->is_btn_touch) {
            uint16_t *start_ptr = (event->code == INPUT_ABS_X) ? &data->start_x : &data->start_y;
            uint16_t *last_ptr = (event->code == INPUT_ABS_X) ? &data->last_x : &data->last_y;
            
            if (*start_ptr == 0xFFFF) *start_ptr = (uint16_t)event->value;
            *last_ptr = (uint16_t)event->value;

            if (!data->session_active && data->start_x != 0xFFFF && data->start_y != 0xFFFF) {
                data->session_active = true;
                LOG_DBG("Session active: initial coords set (%u, %u)", data->start_x, data->start_y);
            }
        }
        k_mutex_unlock(&data->lock);
        return cfg->suppress_abs ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;

    case INPUT_EV_KEY:
        if (event->code == BTN_TOUCH) {
            bool on = (event->value != 0);
            k_mutex_lock(&data->lock, K_FOREVER);
            data->is_btn_touch = on;
            if (on) {
                /* Start of touch (ON): clear all coordinate state */
                data->session_active = false;
                data->start_x = data->start_y = 0xFFFF;
                data->last_x = data->last_y = 0xFFFF;
            }
            LOG_DBG("BTN_TOUCH -> %u", on);
            k_mutex_unlock(&data->lock);

            if (!on) trigger_gesture(dev);
            return cfg->suppress_key ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
        }
        return cfg->suppress_key ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int input_processor_grid_init(const struct device *dev) {
    struct grid_processor_data *data = dev->data;
    const struct grid_processor_config *cfg = dev->config;

    if (cfg->rows == 0 || cfg->columns == 0 || cfg->x == 0 || cfg->y == 0) {
        LOG_ERR("Invalid config: dimensions and ranges must be > 0");
        return -EINVAL;
    }

    if (cfg->cell_count != (cfg->rows * (size_t)cfg->columns)) {
        LOG_ERR("Cell count mismatch: rows=%u columns=%u bindings=%u", cfg->rows, cfg->columns, (uint32_t)cfg->cell_count);
        return -EINVAL;
    }

    k_mutex_init(&data->lock);
    data->config = cfg;
    data->start_x = data->start_y = 0xFFFF;
    data->last_x = data->last_y = 0xFFFF;
    data->session_active = false;
    data->is_btn_touch = false;

    LOG_INF("zip_matrix ready: %ux%u grid", cfg->rows, cfg->columns);
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
        .rows = DT_INST_PROP(n, rows), .columns = DT_INST_PROP(n, columns), \
        .x = DT_INST_PROP(n, x), .y = DT_INST_PROP(n, y), \
        .threshold = DT_INST_PROP(n, threshold), \
        .suppress_abs = DT_INST_PROP(n, suppress_abs), .suppress_key = DT_INST_PROP(n, suppress_key), \
        .cells = processor_grid_cells_##n, .cell_count = ARRAY_SIZE(processor_grid_cells_##n), \
    }; \
    DEVICE_DT_INST_DEFINE(n, input_processor_grid_init, NULL, \
                           &processor_grid_data_##n, &processor_grid_config_##n, \
                           POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                           &grid_processor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GRID_PROCESSOR_INST)
