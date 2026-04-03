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

/**
 * Gesture Enumeration
 * Rows in the virtual matrix are divided by grid_rows per gesture.
 */
enum {
    GESTURE_TAP   = 0,
    GESTURE_UP    = 1,
    GESTURE_DOWN  = 2,
    GESTURE_LEFT  = 3,
    GESTURE_RIGHT = 4
};

struct zip_matrix_config {
    uint8_t rows, columns;
    uint16_t x, y, flick_threshold;
    uint16_t long_press_ms;
    bool suppress_abs, suppress_key;
    const struct device *kscan_dev;
};

struct zip_matrix_data {
    struct k_spinlock lock;
    const struct zip_matrix_config *config;
    /* Latest absolute coordinates observed from the pointing device. */
    uint16_t current_x, current_y;
    /* Coordinates latched at touch start, after the first synced ABS report. */
    uint16_t start_x, start_y;
    /* Latest coordinates seen while the touch is active. */
    uint16_t last_x, last_y;
    bool is_btn_touch;
    /* True between BTN_TOUCH press and the first synced ABS snapshot. */
    bool sync_start_pending;
    const struct device *kscan_dev;

    /* Long-press management */
    struct k_work_delayable hold_work;
    bool is_holding;
    uint8_t hold_row;
    uint8_t hold_column;
};

/**
 * @brief Calculate KSCAN matrix coordinates (row/column).
 * @param gesture The detect gesture type (0-4).
 * @param out_row Calculated row: (gesture * rows) + grid_row.
 * @param out_column Target column (preserves physical grid layout).
 */
static void calculate_kscan_coordinates(const struct zip_matrix_config *cfg,
                                        uint16_t x, uint16_t y, uint8_t gesture,
                                        uint8_t *out_row, uint8_t *out_column) {
    uint32_t pos_x = CLAMP(x, 0, cfg->x);
    uint32_t pos_y = CLAMP(y, 0, cfg->y);
    
    /* Calculate 0-indexed grid position */
    uint8_t grid_column = MIN(cfg->columns - 1, (uint8_t)((uint32_t)pos_x * cfg->columns / MAX(1, cfg->x)));
    uint8_t grid_row    = MIN(cfg->rows - 1,    (uint8_t)((uint32_t)pos_y * cfg->rows    / MAX(1, cfg->y)));
    
    /* Map to virtual matrix block */
    *out_row    = (gesture * cfg->rows) + grid_row;
    *out_column = grid_column;
}

/**
 * @brief Determine gesture type based on displacement (dx, dy).
 */
static uint8_t get_gesture_type(const struct zip_matrix_config *cfg, 
                                int32_t dx, int32_t dy) {
    uint32_t abs_dx = (dx < 0) ? -dx : dx;
    uint32_t abs_dy = (dy < 0) ? -dy : dy;

    if (abs_dx < cfg->flick_threshold && abs_dy < cfg->flick_threshold) {
        return GESTURE_TAP;
    }
    
    if (abs_dy > abs_dx) {
        return (dy < 0) ? GESTURE_UP : GESTURE_DOWN;
    } else {
        return (dx < 0) ? GESTURE_LEFT : GESTURE_RIGHT;
    }
}

/**
 * @brief Background handler for long-press (hold) events.
 */
static void hold_work_handler(struct k_work *work) {
    struct zip_matrix_data *data = CONTAINER_OF(work, struct zip_matrix_data, hold_work.work);
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    
    bool trigger = false;
    /* If the finger is already up, release handling won the race and we emit nothing here. */
    if (data->is_btn_touch && !data->is_holding &&
        data->start_x != COORD_UNINITIALIZED && data->start_y != COORD_UNINITIALIZED) {
        /* A hold is always mapped as a TAP gesture at the touch-start cell. */
        calculate_kscan_coordinates(data->config, data->start_x, data->start_y,
                                    GESTURE_TAP, &data->hold_row, &data->hold_column);

        data->is_holding = true;
        trigger = true;
    }
    k_spin_unlock(&data->lock, key);

    if (trigger) {
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)data->hold_row, (uint32_t)data->hold_column, true);
        LOG_DBG("Hold triggered: Row %u, Column %u", data->hold_row, data->hold_column);
    }
}

/**
 * @brief Handle gesture calculation and reporting on touch release.
 */
static void process_gesture(const struct device *kscan_dev, const struct zip_matrix_config *cfg,
                            uint16_t start_x, uint16_t start_y, uint16_t last_x, uint16_t last_y) {
    if (start_x == COORD_UNINITIALIZED || start_y == COORD_UNINITIALIZED ||
        last_x == COORD_UNINITIALIZED || last_y == COORD_UNINITIALIZED) {
        return;
    }

    int32_t dx = (int32_t)last_x - (int32_t)start_x;
    int32_t dy = (int32_t)last_y - (int32_t)start_y;

    uint8_t gesture = get_gesture_type(cfg, dx, dy);
    uint8_t kscan_row, kscan_column;
    
    /*
     * The touched cell is chosen from the touch-start position.
     * Only the gesture block (tap/up/down/left/right) comes from the movement delta.
     */
    calculate_kscan_coordinates(cfg, start_x, start_y, gesture, &kscan_row, &kscan_column);

    LOG_DBG("Gesture detected: Row %u, Column %u (Type=%u)", kscan_row, kscan_column, gesture);

    /* Direct tap/flick event (press and immediate release) */
    zmk_kscan_matrix_report_event(kscan_dev, (uint32_t)kscan_row, (uint32_t)kscan_column, true);
    zmk_kscan_matrix_report_event(kscan_dev, (uint32_t)kscan_row, (uint32_t)kscan_column, false);
}

/**
 * @brief Input event handler for the matrix processor.
 */
static int zip_matrix_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state) {
    struct zip_matrix_data *data = dev->data;
    const struct zip_matrix_config *cfg = data->config;
    k_spinlock_key_t key;
    int ret = ZMK_INPUT_PROC_CONTINUE;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(state);

    switch (event->type) {
    case INPUT_EV_ABS:
        if (event->code != INPUT_ABS_X && event->code != INPUT_ABS_Y) {
            break;
        }

        key = k_spin_lock(&data->lock);
        /* X and Y can arrive as separate events, so keep the latest pair in shared state. */
        if (event->code == INPUT_ABS_X) {
            data->current_x = event->value;
        }
        if (event->code == INPUT_ABS_Y) {
            data->current_y = event->value;
        }

        if (data->is_btn_touch && !data->sync_start_pending) {
            /* Once touch start is latched, ABS updates track the tail of the gesture. */
            data->last_x = data->current_x;
            data->last_y = data->current_y;

            /* Cancel hold timer if movement exceeds threshold */
            if (data->start_x != COORD_UNINITIALIZED && data->start_y != COORD_UNINITIALIZED) {
                int32_t dx = (int32_t)data->current_x - (int32_t)data->start_x;
                int32_t dy = (int32_t)data->current_y - (int32_t)data->start_y;
                uint32_t abs_dx = (dx < 0) ? -dx : dx;
                uint32_t abs_dy = (dy < 0) ? -dy : dy;

                if (!data->is_holding &&
                    (abs_dx >= cfg->flick_threshold || abs_dy >= cfg->flick_threshold)) {
                    k_work_cancel_delayable(&data->hold_work);
                }
            }
        }
        k_spin_unlock(&data->lock, key);

        ret = cfg->suppress_abs ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
        break;

    case INPUT_EV_KEY:
        if (event->code == INPUT_BTN_TOUCH) {
            bool on = event->value;
            k_spinlock_key_t lock_key = k_spin_lock(&data->lock);

            if (on) {
                /*
                 * BTN_TOUCH can arrive before the matching ABS pair is complete.
                 * Delay latching start_x/start_y until the next synced report.
                 */
                data->is_btn_touch = true;
                data->sync_start_pending = true;
                data->is_holding = false;
                k_spin_unlock(&data->lock, lock_key);
            } else {
                /*
                 * Take a snapshot under lock, then emit outside the lock.
                 * That avoids holding the spinlock across KSCAN callbacks.
                 */
                data->is_btn_touch = false;
                data->sync_start_pending = false;

                uint16_t snap_start_x = data->start_x;
                uint16_t snap_start_y = data->start_y;
                uint16_t snap_last_x = data->last_x;
                uint16_t snap_last_y = data->last_y;
                bool snap_holding = data->is_holding;
                uint8_t snap_row = data->hold_row;
                uint8_t snap_column = data->hold_column;

                k_work_cancel_delayable(&data->hold_work);

                data->is_holding = false;
                k_spin_unlock(&data->lock, lock_key);

                if (snap_holding) {
                    /* A long-press already emitted its press event, so release only. */
                    zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)snap_row,
                                                  (uint32_t)snap_column, false);
                    LOG_DBG("Hold released (Row %u, Column %u)", snap_row, snap_column);
                } else {
                    /* Short touch or flick: classify now and emit a press+release pair. */
                    process_gesture(data->kscan_dev, cfg, snap_start_x, snap_start_y, snap_last_x,
                                    snap_last_y);
                }
            }
        }
        ret = cfg->suppress_key ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
        break;
    }

    if (event->sync) {
        key = k_spin_lock(&data->lock);
        /*
         * Use the first synced ABS frame after BTN_TOUCH as the canonical touch-start point.
         * This keeps X/Y paired and avoids mixing an old X with a new Y (or vice versa).
         */
        if (data->sync_start_pending && data->current_x != COORD_UNINITIALIZED &&
            data->current_y != COORD_UNINITIALIZED) {
            data->start_x = data->current_x;
            data->start_y = data->current_y;
            data->last_x = data->current_x;
            data->last_y = data->current_y;
            data->sync_start_pending = false;

            if (cfg->long_press_ms > 0) {
                /* The hold timer starts only after touch-start coordinates are stable. */
                k_work_reschedule(&data->hold_work, K_MSEC(cfg->long_press_ms));
            }
        }
        k_spin_unlock(&data->lock, key);
    }

    return ret;
}

static int zip_matrix_init(const struct device *dev) {
    struct zip_matrix_data *data = dev->data;
    const struct zip_matrix_config *cfg = dev->config;

    if (cfg->rows == 0 || cfg->columns == 0 || cfg->x == 0 || cfg->y == 0) {
        LOG_ERR("Invalid grid configuration: %u x %u (Max X=%u, Y=%u)", 
                cfg->rows, cfg->columns, cfg->x, cfg->y);
        return -EINVAL;
    }

    data->config = cfg;
    data->current_x = data->current_y = COORD_UNINITIALIZED;
    data->start_x   = data->start_y   = COORD_UNINITIALIZED;
    data->last_x    = data->last_y    = COORD_UNINITIALIZED;
    data->is_btn_touch = false;
    data->sync_start_pending = false;
    data->is_holding = false;
    
    k_work_init_delayable(&data->hold_work, hold_work_handler);

    if (!device_is_ready(cfg->kscan_dev)) {
        LOG_ERR("KSCAN proxy device not ready");
        return -ENODEV;
    }
    data->kscan_dev = cfg->kscan_dev;

    LOG_INF("zip_matrix initialized: %ux%u grid", cfg->rows, cfg->columns);
    return 0;
}

static const struct zmk_input_processor_driver_api zip_matrix_driver_api = {
    .handle_event = zip_matrix_handle_event,
};

#define ZIP_MATRIX_INST(n) \
    static struct zip_matrix_data zip_matrix_data_##n = {}; \
    static const struct zip_matrix_config zip_matrix_config_##n = { \
        .rows = DT_INST_PROP(n, rows), .columns = DT_INST_PROP(n, columns), \
        .x = DT_INST_PROP(n, x), .y = DT_INST_PROP(n, y), \
        .flick_threshold = DT_INST_PROP(n, flick_threshold), \
        .long_press_ms   = DT_INST_PROP(n, long_press_ms), \
        .suppress_abs = DT_INST_PROP(n, suppress_abs), .suppress_key = DT_INST_PROP(n, suppress_key), \
        .kscan_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)), \
    }; \
    DEVICE_DT_INST_DEFINE(n, zip_matrix_init, NULL, \
                          &zip_matrix_data_##n, &zip_matrix_config_##n, \
                          POST_KERNEL, 41, \
                          &zip_matrix_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_INST)
