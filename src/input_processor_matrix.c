/*
 * Copyright (c) 2025 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_matrix

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/spinlock.h>
#include <drivers/input_processor.h>
#include <kscan_input_matrix.h>

LOG_MODULE_REGISTER(zip_matrix, CONFIG_ZMK_LOG_LEVEL);

#define COORD_UNINITIALIZED UINT16_MAX
#define COORD_INVALID_ZERO  0xFFF

enum gesture_type {
    GESTURE_TAP   = 0,
    GESTURE_UP    = 1,
    GESTURE_DOWN  = 2,
    GESTURE_LEFT  = 3,
    GESTURE_RIGHT = 4,
};

struct zip_matrix_config {
    uint8_t rows;
    uint8_t columns;
    uint16_t x;
    uint16_t y;
    uint16_t flick_threshold;
    uint16_t long_press_ms;
    bool suppress_abs;
    bool suppress_key;
    const struct device *kscan_dev;
};

struct zip_matrix_data {
    struct k_spinlock lock;
    const struct zip_matrix_config *config;
    uint16_t current_x;
    uint16_t current_y;
    bool is_btn_touch;
    uint16_t start_x;
    uint16_t start_y;
    const struct device *kscan_dev;
    struct k_work_delayable hold_work;
    bool is_holding;
    uint8_t hold_row;
    uint8_t hold_column;
};

static void calculate_kscan_coordinates(const struct zip_matrix_config *cfg,
                                        uint16_t x, uint16_t y,
                                        enum gesture_type gesture,
                                        uint8_t *out_row, uint8_t *out_column)
{
    uint32_t px = CLAMP(x, 0, cfg->x), py = CLAMP(y, 0, cfg->y);
    *out_row    = ((uint8_t)gesture * cfg->rows) + MIN(cfg->rows - 1, (uint8_t)(py * cfg->rows / MAX(1U, cfg->y)));
    *out_column = MIN(cfg->columns - 1, (uint8_t)(px * cfg->columns / MAX(1U, cfg->x)));
}

static enum gesture_type get_gesture_type(const struct zip_matrix_config *cfg, int32_t dx, int32_t dy)
{
    uint32_t adx = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t ady = (uint32_t)(dy < 0 ? -dy : dy);
    if (adx < cfg->flick_threshold && ady < cfg->flick_threshold) return GESTURE_TAP;
    return (ady > adx) ? (dy < 0 ? GESTURE_UP : GESTURE_DOWN) : (dx < 0 ? GESTURE_LEFT : GESTURE_RIGHT);
}

static void hold_work_handler(struct k_work *work)
{
    struct zip_matrix_data *data = CONTAINER_OF(work, struct zip_matrix_data, hold_work.work);
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool trigger = false; uint8_t r, c;
    if (data->is_btn_touch && !data->is_holding && data->start_x != COORD_UNINITIALIZED) {
        calculate_kscan_coordinates(data->config, data->start_x, data->start_y, get_gesture_type(data->config, (int32_t)data->current_x - (int32_t)data->start_x, (int32_t)data->current_y - (int32_t)data->start_y), &data->hold_row, &data->hold_column);
        data->is_holding = true; r = data->hold_row; c = data->hold_column; trigger = true;
    }
    k_spin_unlock(&data->lock, key);
    if (trigger) {
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, true);
        k_spinlock_key_t k2 = k_spin_lock(&data->lock);
        bool orphaned = !data->is_btn_touch && !data->is_holding;
        k_spin_unlock(&data->lock, k2);
        if (orphaned) zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, false);
    }
}

static int zip_matrix_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state)
{
    struct zip_matrix_data *data = dev->data; const struct zip_matrix_config *cfg = data->config;
    k_spinlock_key_t key; int ret = ZMK_INPUT_PROC_CONTINUE;
    bool is_sync = event->sync;

    switch (event->type) {
    case INPUT_EV_ABS:
        if (event->code == INPUT_ABS_X || event->code == INPUT_ABS_Y) {
            key = k_spin_lock(&data->lock);
            if (event->code == INPUT_ABS_X) data->current_x = (uint16_t)event->value;
            else data->current_y = (uint16_t)event->value;
            k_spin_unlock(&data->lock, key);
        }
        if (cfg->suppress_abs) { event->code = COORD_INVALID_ZERO; event->sync = false; ret = ZMK_INPUT_PROC_STOP; }
        break;
    case INPUT_EV_KEY:
        if (event->code == INPUT_BTN_TOUCH) {
            bool on = (bool)event->value; key = k_spin_lock(&data->lock);
            if (on) {
                k_work_cancel_delayable(&data->hold_work);
                bool stale_h = data->is_holding; uint8_t sr = data->hold_row, sc = data->hold_column;
                data->is_btn_touch = true; data->is_holding = false; data->start_x = data->start_y = COORD_UNINITIALIZED;
                k_spin_unlock(&data->lock, key);
                if (stale_h) zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)sr, (uint32_t)sc, false);
            } else { data->is_btn_touch = false; k_spin_unlock(&data->lock, key); }
        }
        if (cfg->suppress_key) { event->code = COORD_INVALID_ZERO; event->sync = false; ret = ZMK_INPUT_PROC_STOP; }
        break;
    }

    if (is_sync) {
        key = k_spin_lock(&data->lock);
        if (data->is_btn_touch) {
            if (data->start_x == COORD_UNINITIALIZED && data->current_x != COORD_UNINITIALIZED && data->current_y != COORD_UNINITIALIZED) {
                data->start_x = data->current_x; data->start_y = data->current_y;
                if (cfg->long_press_ms > 0) k_work_reschedule(&data->hold_work, K_MSEC(cfg->long_press_ms));
            } else if (data->start_x != COORD_UNINITIALIZED && !data->is_holding) {
                int32_t dx = (int32_t)data->current_x - (int32_t)data->start_x;
                int32_t dy = (int32_t)data->current_y - (int32_t)data->start_y;
                uint32_t adx = (uint32_t)(dx < 0 ? -dx : dx);
                uint32_t ady = (uint32_t)(dy < 0 ? -dy : dy);
                if (adx >= cfg->flick_threshold || ady >= cfg->flick_threshold) {
                    k_work_cancel_delayable(&data->hold_work);
                }
            }
        } else if (data->start_x != COORD_UNINITIALIZED) {
            k_work_cancel_delayable(&data->hold_work);
            bool held = data->is_holding; uint8_t r = data->hold_row, c = data->hold_column;
            uint16_t sx = data->start_x, sy = data->start_y, cx = data->current_x, cy = data->current_y;
            data->start_x = data->start_y = COORD_UNINITIALIZED; data->is_holding = false;
            k_spin_unlock(&data->lock, key);
            if (held) zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, false);
            else {
                uint8_t rr, cc; calculate_kscan_coordinates(cfg, sx, sy, get_gesture_type(cfg, (int32_t)cx - (int32_t)sx, (int32_t)cy - (int32_t)sy), &rr, &cc);
                zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)rr, (uint32_t)cc, true);
                zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)rr, (uint32_t)cc, false);
            }
            return ret;
        }
        k_spin_unlock(&data->lock, key);
    }
    return ret;
}

static int zip_matrix_init(const struct device *dev)
{
    struct zip_matrix_data *data = dev->data; const struct zip_matrix_config *cfg = dev->config;
    if (cfg->rows == 0 || cfg->columns == 0 || cfg->x == 0 || cfg->y == 0) return -EINVAL;
    data->config = cfg; data->current_x = data->current_y = data->start_x = data->start_y = COORD_UNINITIALIZED;
    data->is_btn_touch = data->is_holding = false;
    k_work_init_delayable(&data->hold_work, hold_work_handler);
    if (!device_is_ready(cfg->kscan_dev)) return -ENODEV;
    data->kscan_dev = cfg->kscan_dev;
    return 0;
}

static const struct zmk_input_processor_driver_api zip_matrix_driver_api = { .handle_event = zip_matrix_handle_event };

#define ZIP_MATRIX_INST(n) \
    static struct zip_matrix_data zip_matrix_data_##n = {}; \
    static const struct zip_matrix_config zip_matrix_config_##n = { \
        .rows = DT_INST_PROP(n, rows), .columns = DT_INST_PROP(n, columns), \
        .x = DT_INST_PROP(n, x), .y = DT_INST_PROP(n, y), \
        .flick_threshold = DT_INST_PROP(n, flick_threshold), .long_press_ms = DT_INST_PROP(n, long_press_ms), \
        .suppress_abs = DT_INST_PROP(n, suppress_abs), .suppress_key = DT_INST_PROP(n, suppress_key), \
        .kscan_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)), \
    }; \
    DEVICE_DT_INST_DEFINE(n, zip_matrix_init, NULL, &zip_matrix_data_##n, &zip_matrix_config_##n, POST_KERNEL, 41, &zip_matrix_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_INST)
