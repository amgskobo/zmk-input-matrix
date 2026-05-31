/*
 * Copyright (c) 2025 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_matrix

#include <stdint.h>
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
#define ZIP_MATRIX_INIT_PRIORITY UTIL_INC(CONFIG_KSCAN_INIT_PRIORITY)
#define ZIP_MATRIX_GESTURE_COUNT 5U
#define ZIP_MATRIX_MAX_GRID_ROWS (UINT8_MAX / ZIP_MATRIX_GESTURE_COUNT)
#define ZIP_MATRIX_MAX_GRID_COLUMNS UINT8_MAX
#define ZIP_MATRIX_MAX_COORD (UINT16_MAX - 1U)
#define ZIP_MATRIX_MAX_U16 UINT16_MAX

#define ZIP_MATRIX_USE_DIAMOND_TAP DT_ANY_INST_HAS_PROP_STATUS_OKAY(diamond_tap)

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
    bool suppress_touch;
    bool suppress_key;
#if ZIP_MATRIX_USE_DIAMOND_TAP
    bool diamond_tap;
#endif
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
    bool hold_reported;
    bool hold_release_pending;
    bool flick_latched;
    enum gesture_type flick_gesture;
    uint8_t hold_row;
    uint8_t hold_column;
    uint32_t contact_id;
    uint32_t hold_contact_id;
};

static uint16_t clamp_coord_value(int32_t value, uint16_t max)
{
    if (value <= 0) {
        return 0;
    }

    if ((uint32_t)value >= max) {
        return max;
    }

    return (uint16_t)value;
}

#if ZIP_MATRIX_USE_DIAMOND_TAP
static uint8_t calculate_diamond_column(const struct zip_matrix_config *cfg,
                                        uint16_t px, uint16_t py)
{
    /*
     * Diamond partitioning for a 1x4 Tap grid.
     * Choose the nearest cardinal key center after normalizing the touch area:
     *   col 0 = Up, col 1 = Right, col 2 = Down, col 3 = Left.
     *
     * The diagonal boundaries are equivalent to comparing normalized distance
     * from the center on each axis:
     *   vertical when |2*py - y| / y >= |2*px - x| / x
     *
     * Cross-multiply to avoid floating point. Ties prefer the vertical axis.
     */
    int32_t dx = ((int32_t)px * 2) - (int32_t)cfg->x;
    int32_t dy = ((int32_t)py * 2) - (int32_t)cfg->y;
    uint32_t abs_dx = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t abs_dy = (uint32_t)(dy < 0 ? -dy : dy);
    uint32_t horizontal = abs_dx * cfg->y;
    uint32_t vertical = abs_dy * cfg->x;

    if (vertical >= horizontal) {
        return (dy < 0) ? 0U : 2U;
    }

    return (dx >= 0) ? 1U : 3U;
}
#endif

static void calculate_kscan_coordinates(const struct zip_matrix_config *cfg,
                                        uint16_t x, uint16_t y,
                                        enum gesture_type gesture,
                                        uint8_t *out_row, uint8_t *out_column)
{
    uint32_t px = MIN((uint32_t)x, (uint32_t)cfg->x);
    uint32_t py = MIN((uint32_t)y, (uint32_t)cfg->y);
    uint8_t grid_row;
    uint8_t grid_column;

#if ZIP_MATRIX_USE_DIAMOND_TAP
    if (gesture == GESTURE_TAP && cfg->diamond_tap &&
        cfg->rows == 1U && cfg->columns == 4U) {
        grid_row = 0U;
        grid_column = calculate_diamond_column(cfg, (uint16_t)px, (uint16_t)py);
    } else
#endif
    {
        grid_row = (cfg->rows == 1U) ? 0U :
                   MIN(cfg->rows - 1U, (uint8_t)(py * cfg->rows / cfg->y));
        grid_column = (cfg->columns == 1U) ? 0U :
                      MIN(cfg->columns - 1U, (uint8_t)(px * cfg->columns / cfg->x));
    }

    *out_row = ((uint8_t)gesture * cfg->rows) + grid_row;
    *out_column = grid_column;
}

static enum gesture_type get_gesture_type(const struct zip_matrix_config *cfg, int32_t dx, int32_t dy)
{
    uint32_t adx = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t ady = (uint32_t)(dy < 0 ? -dy : dy);

    if (adx < cfg->flick_threshold && ady < cfg->flick_threshold) {
        return GESTURE_TAP;
    }

    return (ady > adx) ? (dy < 0 ? GESTURE_UP : GESTURE_DOWN) : (dx < 0 ? GESTURE_LEFT : GESTURE_RIGHT);
}

static void hold_work_handler(struct k_work *work)
{
    struct zip_matrix_data *data = CONTAINER_OF(work, struct zip_matrix_data, hold_work.work);
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool trigger = false;
    uint8_t r = 0;
    uint8_t c = 0;
    uint32_t work_contact_id = 0;

    if (data->is_btn_touch && !data->is_holding && !data->flick_latched &&
        data->start_x != COORD_UNINITIALIZED) {
        calculate_kscan_coordinates(data->config, data->start_x, data->start_y, GESTURE_TAP,
                                    &data->hold_row, &data->hold_column);
        data->is_holding = true;
        data->hold_reported = false;
        data->hold_release_pending = false;
        data->hold_contact_id = data->contact_id;
        r = data->hold_row;
        c = data->hold_column;
        work_contact_id = data->hold_contact_id;
        trigger = true;
    }

    k_spin_unlock(&data->lock, key);

    if (trigger) {
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, true);
        k_spinlock_key_t k2 = k_spin_lock(&data->lock);
        bool same_hold = data->is_holding && data->hold_contact_id == work_contact_id;
        bool same_contact = data->contact_id == work_contact_id;
        bool release_after_press = !same_hold;

        if (same_hold) {
            data->hold_reported = true;
            release_after_press = data->hold_release_pending || !data->is_btn_touch || !same_contact;
        }

        if (same_hold && release_after_press) {
            data->is_holding = false;
            data->hold_reported = false;
            data->hold_release_pending = false;
            if (same_contact) {
                data->start_x = COORD_UNINITIALIZED;
                data->start_y = COORD_UNINITIALIZED;
                data->flick_latched = false;
                data->flick_gesture = GESTURE_TAP;
                if (!data->is_btn_touch) {
                    data->current_x = COORD_UNINITIALIZED;
                    data->current_y = COORD_UNINITIALIZED;
                }
            }
        }
        k_spin_unlock(&data->lock, k2);

        if (release_after_press) {
            zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, false);
        }
    }
}

static int zip_matrix_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state)
{
    struct zip_matrix_data *data = dev->data;
    const struct zip_matrix_config *cfg = data->config;
    int ret = ZMK_INPUT_PROC_CONTINUE;
    bool is_sync = event->sync;
    bool cancel_hold = false;
    bool schedule_hold = false;
    bool release_stale_hold = false;
    bool report_release = false;
    bool report_gesture = false;
    uint8_t release_row = 0;
    uint8_t release_column = 0;
    uint8_t gesture_row = 0;
    uint8_t gesture_column = 0;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(state);

    switch (event->type) {
    case INPUT_EV_ABS:
        if (event->code == INPUT_ABS_X || event->code == INPUT_ABS_Y) {
            k_spinlock_key_t key = k_spin_lock(&data->lock);
            if (event->code == INPUT_ABS_X) {
                data->current_x = clamp_coord_value(event->value, cfg->x);
            } else {
                data->current_y = clamp_coord_value(event->value, cfg->y);
            }
            k_spin_unlock(&data->lock, key);
        }
        if (cfg->suppress_abs) {
            event->code = COORD_INVALID_ZERO;
            event->sync = false;
            ret = ZMK_INPUT_PROC_STOP;
        }
        break;
    case INPUT_EV_KEY:
        if (event->code == INPUT_BTN_TOUCH) {
            bool on = (bool)event->value;
            k_spinlock_key_t key = k_spin_lock(&data->lock);
            bool was_touch = data->is_btn_touch;

            if (on && !was_touch) {
                cancel_hold = true;
                release_stale_hold = data->is_holding && data->hold_reported;
                release_row = data->hold_row;
                release_column = data->hold_column;
                if (data->is_holding && !data->hold_reported) {
                    data->hold_release_pending = true;
                } else {
                    data->is_holding = false;
                    data->hold_reported = false;
                    data->hold_release_pending = false;
                }
                data->contact_id++;
                data->is_btn_touch = true;
                data->flick_latched = false;
                data->flick_gesture = GESTURE_TAP;
                data->start_x = COORD_UNINITIALIZED;
                data->start_y = COORD_UNINITIALIZED;
            } else if (!on && was_touch) {
                data->is_btn_touch = false;
            }

            k_spin_unlock(&data->lock, key);
        }
        if (cfg->suppress_key || (cfg->suppress_touch && event->code == INPUT_BTN_TOUCH)) {
            event->code = COORD_INVALID_ZERO;
            event->sync = false;
            ret = ZMK_INPUT_PROC_STOP;
        }
        break;
    }

    if (is_sync) {
        k_spinlock_key_t key = k_spin_lock(&data->lock);

        if (data->is_btn_touch) {
            if (data->start_x == COORD_UNINITIALIZED && data->current_x != COORD_UNINITIALIZED && data->current_y != COORD_UNINITIALIZED) {
                data->start_x = data->current_x;
                data->start_y = data->current_y;
                schedule_hold = cfg->long_press_ms > 0;
            } else if (data->start_x != COORD_UNINITIALIZED &&
                       !(data->is_holding && data->hold_contact_id == data->contact_id) &&
                       !data->flick_latched) {
                int32_t dx = (int32_t)data->current_x - (int32_t)data->start_x;
                int32_t dy = (int32_t)data->current_y - (int32_t)data->start_y;
                uint32_t adx = (uint32_t)(dx < 0 ? -dx : dx);
                uint32_t ady = (uint32_t)(dy < 0 ? -dy : dy);

                if (adx >= cfg->flick_threshold || ady >= cfg->flick_threshold) {
                    data->flick_latched = true;
                    data->flick_gesture = get_gesture_type(cfg, dx, dy);
                    cancel_hold = true;
                }
            }
        } else if (data->start_x != COORD_UNINITIALIZED) {
            cancel_hold = true;
            bool held = data->is_holding && data->hold_contact_id == data->contact_id;
            bool stale_hold = data->is_holding && data->hold_contact_id != data->contact_id;
            bool hold_reported = held && data->hold_reported;
            bool flick_latched = data->flick_latched;
            enum gesture_type gesture = data->flick_gesture;
            uint8_t r = data->hold_row;
            uint8_t c = data->hold_column;
            uint16_t sx = data->start_x, sy = data->start_y, cx = data->current_x, cy = data->current_y;

            data->start_x = COORD_UNINITIALIZED;
            data->start_y = COORD_UNINITIALIZED;
            data->current_x = COORD_UNINITIALIZED;
            data->current_y = COORD_UNINITIALIZED;
            data->flick_latched = false;
            data->flick_gesture = GESTURE_TAP;

            if (held && !hold_reported) {
                data->hold_release_pending = true;
            } else if (held || !stale_hold) {
                data->is_holding = false;
                data->hold_reported = false;
                data->hold_release_pending = false;
            }
            k_spin_unlock(&data->lock, key);

            if (held && hold_reported) {
                release_row = r;
                release_column = c;
                report_release = true;
            } else if (!held) {
                if (!flick_latched) {
                    gesture = get_gesture_type(cfg, (int32_t)cx - (int32_t)sx, (int32_t)cy - (int32_t)sy);
                }

                calculate_kscan_coordinates(cfg, sx, sy, gesture, &gesture_row, &gesture_column);
                report_gesture = true;
            }

            goto report_events;
        } else if (data->current_x != COORD_UNINITIALIZED || data->current_y != COORD_UNINITIALIZED) {
            data->current_x = COORD_UNINITIALIZED;
            data->current_y = COORD_UNINITIALIZED;
        }

        k_spin_unlock(&data->lock, key);
    }

report_events:
    if (cancel_hold) {
        k_work_cancel_delayable(&data->hold_work);
    }

    if (schedule_hold) {
        k_work_reschedule(&data->hold_work, K_MSEC(cfg->long_press_ms));
    }

    if (release_stale_hold || report_release) {
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)release_row, (uint32_t)release_column,
                                      false);
    }

    if (report_gesture) {
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)gesture_row, (uint32_t)gesture_column,
                                      true);
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)gesture_row, (uint32_t)gesture_column,
                                      false);
    }

    return ret;
}

static int zip_matrix_init(const struct device *dev)
{
    struct zip_matrix_data *data = dev->data;
    const struct zip_matrix_config *cfg = dev->config;

    if (cfg->rows == 0 || cfg->columns == 0 || cfg->x == 0 || cfg->y == 0) return -EINVAL;

    data->config = cfg;
    data->current_x = COORD_UNINITIALIZED;
    data->current_y = COORD_UNINITIALIZED;
    data->start_x = COORD_UNINITIALIZED;
    data->start_y = COORD_UNINITIALIZED;
    data->is_btn_touch = false;
    data->is_holding = false;
    data->hold_reported = false;
    data->hold_release_pending = false;
    data->flick_latched = false;
    data->flick_gesture = GESTURE_TAP;
    data->contact_id = 0;
    data->hold_contact_id = 0;
    k_work_init_delayable(&data->hold_work, hold_work_handler);

    data->kscan_dev = cfg->kscan_dev;
    return 0;
}

static const struct zmk_input_processor_driver_api zip_matrix_driver_api = { .handle_event = zip_matrix_handle_event };

#define ZIP_MATRIX_KSCAN_NODE(n) DT_INST_PHANDLE(n, kscan)

#define ZIP_MATRIX_VALIDATE_INST(n) \
    BUILD_ASSERT(DT_NODE_HAS_STATUS(ZIP_MATRIX_KSCAN_NODE(n), okay), \
                 "zmk,input-processor-matrix kscan phandle must reference an okay device"); \
    BUILD_ASSERT(DT_NODE_HAS_COMPAT(ZIP_MATRIX_KSCAN_NODE(n), zmk_kscan_input_matrix), \
                 "zmk,input-processor-matrix kscan phandle must reference zmk,kscan-input-matrix"); \
    BUILD_ASSERT(DT_INST_PROP(n, rows) > 0, "zmk,input-processor-matrix rows must be greater than zero"); \
    BUILD_ASSERT(DT_INST_PROP(n, rows) <= ZIP_MATRIX_MAX_GRID_ROWS, \
                 "zmk,input-processor-matrix rows must be <= 51"); \
    BUILD_ASSERT(DT_INST_PROP(n, columns) > 0, \
                 "zmk,input-processor-matrix columns must be greater than zero"); \
    BUILD_ASSERT(DT_INST_PROP(n, columns) <= ZIP_MATRIX_MAX_GRID_COLUMNS, \
                 "zmk,input-processor-matrix columns must be <= 255"); \
    BUILD_ASSERT(DT_INST_PROP(n, x) > 0 && DT_INST_PROP(n, x) <= ZIP_MATRIX_MAX_COORD, \
                 "zmk,input-processor-matrix x must be between 1 and 65534"); \
    BUILD_ASSERT(DT_INST_PROP(n, y) > 0 && DT_INST_PROP(n, y) <= ZIP_MATRIX_MAX_COORD, \
                 "zmk,input-processor-matrix y must be between 1 and 65534"); \
    BUILD_ASSERT(DT_INST_PROP(n, flick_threshold) > 0 && \
                     DT_INST_PROP(n, flick_threshold) <= ZIP_MATRIX_MAX_U16, \
                 "zmk,input-processor-matrix flick-threshold must be between 1 and 65535"); \
    BUILD_ASSERT(DT_INST_PROP(n, long_press_ms) <= ZIP_MATRIX_MAX_U16, \
                 "zmk,input-processor-matrix long-press-ms must be <= 65535"); \
    BUILD_ASSERT(DT_PROP_OR(ZIP_MATRIX_KSCAN_NODE(n), rows, 0) == \
                     (ZIP_MATRIX_GESTURE_COUNT * DT_INST_PROP(n, rows)), \
                 "zmk,kscan-input-matrix rows must equal 5 * zmk,input-processor-matrix rows"); \
    BUILD_ASSERT(DT_PROP_OR(ZIP_MATRIX_KSCAN_NODE(n), columns, 0) == DT_INST_PROP(n, columns), \
                 "zmk,kscan-input-matrix columns must equal zmk,input-processor-matrix columns"); \
    ZIP_MATRIX_VALIDATE_DIAMOND_TAP(n)

#if ZIP_MATRIX_USE_DIAMOND_TAP
#define ZIP_MATRIX_VALIDATE_DIAMOND_TAP(n) \
    BUILD_ASSERT(!DT_INST_PROP(n, diamond_tap) || \
                     (DT_INST_PROP(n, rows) == 1 && DT_INST_PROP(n, columns) == 4), \
                 "diamond-tap requires rows = 1 and columns = 4");
#define ZIP_MATRIX_DIAMOND_TAP_FIELD(n) \
        .diamond_tap = DT_INST_PROP(n, diamond_tap),
#else
#define ZIP_MATRIX_VALIDATE_DIAMOND_TAP(n)
#define ZIP_MATRIX_DIAMOND_TAP_FIELD(n)
#endif

#define ZIP_MATRIX_INST(n) \
    ZIP_MATRIX_VALIDATE_INST(n) \
    static struct zip_matrix_data zip_matrix_data_##n = {}; \
    static const struct zip_matrix_config zip_matrix_config_##n = { \
        .rows = DT_INST_PROP(n, rows), .columns = DT_INST_PROP(n, columns), \
        .x = DT_INST_PROP(n, x), .y = DT_INST_PROP(n, y), \
        .flick_threshold = DT_INST_PROP(n, flick_threshold), .long_press_ms = DT_INST_PROP(n, long_press_ms), \
        .suppress_abs = DT_INST_PROP(n, suppress_abs), .suppress_touch = DT_INST_PROP(n, suppress_touch), \
        .suppress_key = DT_INST_PROP(n, suppress_key), \
        ZIP_MATRIX_DIAMOND_TAP_FIELD(n) \
        .kscan_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)), \
    }; \
    DEVICE_DT_INST_DEFINE(n, zip_matrix_init, NULL, &zip_matrix_data_##n, &zip_matrix_config_##n, POST_KERNEL, ZIP_MATRIX_INIT_PRIORITY, &zip_matrix_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_INST)
