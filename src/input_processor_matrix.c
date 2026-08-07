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
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

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
    /*
     * Squared travel of the farthest point reached so far, so the direction can
     * be re-read from the longest vector instead of the short one that happened
     * to cross the threshold first.
     */
    uint64_t flick_max_travel;
    uint8_t hold_row;
    uint8_t hold_column;
    uint32_t contact_id;
    uint32_t hold_contact_id;
    /*
     * One bit per INPUT_BTN_0..INPUT_BTN_15 whose press was suppressed here and
     * whose release has not been seen yet, plus when that record was last
     * updated.
     */
    uint16_t suppressed_btns;
    uint32_t suppressed_at;
};

#define ZIP_MATRIX_TRACKED_BTNS 16

/*
 * How long a suppressed press stays on record. When the layer changes after a
 * press was suppressed here, the matching release is routed elsewhere and never
 * comes back to clear it. Without an expiry that stale bit would suppress an
 * unrelated later release - exactly the stuck button this tracking exists to
 * prevent. Generated click sequences are far shorter than this.
 */
#define ZIP_MATRIX_SUPPRESS_TTL_MS 500

/*
 * Decide whether a KEY event may be consumed.
 *
 * Suppression has to stay paired. Which processors run is decided per event
 * from the layer active at that moment, so a button press and its release can
 * be routed differently when the layer changes in between. Dropping a release
 * whose press was never suppressed here leaves that button held down on the
 * host with nothing left to release it, so a release is only dropped when it
 * matches a press that was dropped here.
 *
 * Passing a release through is always safe: it cannot produce a stray press,
 * because the press it belongs to was already delivered.
 *
 * BTN_TOUCH is excluded - this processor keeps its own touch state and consumes
 * both edges unconditionally.
 */
static bool zip_matrix_may_suppress_key(struct zip_matrix_data *data,
                                        const struct input_event *event)
{
    if (event->code == INPUT_BTN_TOUCH ||
        event->code < INPUT_BTN_0 ||
        event->code >= INPUT_BTN_0 + ZIP_MATRIX_TRACKED_BTNS) {
        return true;
    }

    uint16_t bit = BIT(event->code - INPUT_BTN_0);
    uint32_t now = k_uptime_get_32();

    if (data->suppressed_btns &&
        (uint32_t)(now - data->suppressed_at) > ZIP_MATRIX_SUPPRESS_TTL_MS) {
        data->suppressed_btns = 0;
    }

    if (event->value) {
        data->suppressed_btns |= bit;
        data->suppressed_at = now;
        return true;
    }

    if (!(data->suppressed_btns & bit)) {
        LOG_WRN("Passing BTN_%d release: its press was not suppressed here",
                event->code - INPUT_BTN_0);
        return false;
    }

    data->suppressed_btns &= ~bit;
    return true;
}

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
    uint64_t abs_dx = (uint64_t)(dx < 0 ? -dx : dx);
    uint64_t abs_dy = (uint64_t)(dy < 0 ? -dy : dy);
    uint64_t horizontal = abs_dx * cfg->y;
    uint64_t vertical = abs_dy * cfg->x;

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
    /*
     * Taps only. The diamond reads a direction out of where the finger rests,
     * which is the opposite of what a flick measures, and on a pad this small a
     * stroke has to begin on the far side of the one it travels towards - so the
     * region a flick starts in is close to a restatement of its row. The
     * rectangular split keeps the column reporting something the row does not.
     */
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

/*
 * Squared travel distance, widened before multiplying. A resolution may be set
 * as high as ZIP_MATRIX_MAX_COORD, and squaring a delta that large overflows 32
 * bits well before the sum is formed.
 */
static uint64_t travel_squared(int32_t dx, int32_t dy)
{
    uint64_t adx = (uint64_t)(dx < 0 ? -(int64_t)dx : (int64_t)dx);
    uint64_t ady = (uint64_t)(dy < 0 ? -(int64_t)dy : (int64_t)dy);

    return (adx * adx) + (ady * ady);
}

static uint64_t flick_threshold_squared(const struct zip_matrix_config *cfg)
{
    return (uint64_t)cfg->flick_threshold * (uint64_t)cfg->flick_threshold;
}

/*
 * A diamond instance takes its direction from the spot the finger came to rest
 * on, so travel carries no direction for it to read - a stroke on a pad this
 * small has to run away from the region it started in, leaving the two readings
 * pointing opposite ways. Such an instance reports taps only, which also frees
 * the board from carrying four gesture rows it can never reach.
 */
static bool flick_enabled(const struct zip_matrix_config *cfg)
{
#if ZIP_MATRIX_USE_DIAMOND_TAP
    return !cfg->diamond_tap;
#else
    ARG_UNUSED(cfg);
    return true;
#endif
}

static enum gesture_type get_gesture_type(const struct zip_matrix_config *cfg, int32_t dx, int32_t dy)
{
    uint64_t adx = (uint64_t)(dx < 0 ? -(int64_t)dx : (int64_t)dx);
    uint64_t ady = (uint64_t)(dy < 0 ? -(int64_t)dy : (int64_t)dy);

    if (!flick_enabled(cfg)) {
        return GESTURE_TAP;
    }

    /*
     * Compare true distance rather than each axis on its own. Testing the axes
     * separately describes a square, so a diagonal travel had to be sqrt(2)
     * longer than an axis-aligned one before it counted as a flick.
     */
    if (travel_squared(dx, dy) < flick_threshold_squared(cfg)) {
        return GESTURE_TAP;
    }

    /*
     * Same boundary the diamond uses: normalise each axis by the resolution and
     * let a tie fall to the vertical. Comparing adx and ady directly would put
     * the split at 45 degrees in raw counts and disagree with the diamond on a
     * pad whose axes differ.
     */
    if (ady * cfg->x >= adx * cfg->y) {
        return (dy < 0) ? GESTURE_UP : GESTURE_DOWN;
    }

    return (dx < 0) ? GESTURE_LEFT : GESTURE_RIGHT;
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
                data->flick_max_travel = 0U;
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
                data->flick_max_travel = 0U;
                data->start_x = COORD_UNINITIALIZED;
                data->start_y = COORD_UNINITIALIZED;
            } else if (!on && was_touch) {
                data->is_btn_touch = false;
            }

            k_spin_unlock(&data->lock, key);
        }
        if ((cfg->suppress_key || (cfg->suppress_touch && event->code == INPUT_BTN_TOUCH)) &&
            zip_matrix_may_suppress_key(data, event)) {
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
            } else if (flick_enabled(cfg) && data->start_x != COORD_UNINITIALIZED &&
                       !(data->is_holding && data->hold_contact_id == data->contact_id)) {
                int32_t dx = (int32_t)data->current_x - (int32_t)data->start_x;
                int32_t dy = (int32_t)data->current_y - (int32_t)data->start_y;
                uint64_t travel = travel_squared(dx, dy);

                /*
                 * Keep following the contact after it qualifies. The first
                 * sample past the threshold is the shortest vector of the whole
                 * stroke and the least trustworthy one to read a direction
                 * from, so hold on to the farthest point instead. The latch
                 * itself still fires once, to drop the pending hold.
                 */
                if (travel >= flick_threshold_squared(cfg) &&
                    travel > data->flick_max_travel) {
                    cancel_hold = !data->flick_latched;
                    data->flick_latched = true;
                    data->flick_max_travel = travel;
                    data->flick_gesture = get_gesture_type(cfg, dx, dy);
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
            data->flick_max_travel = 0U;

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
    data->flick_max_travel = 0U;
    data->contact_id = 0;
    data->hold_contact_id = 0;
    data->suppressed_btns = 0;
    data->suppressed_at = 0;
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
                     (ZIP_MATRIX_GESTURE_ROWS(n) * DT_INST_PROP(n, rows)), \
                 "zmk,kscan-input-matrix rows must equal zmk,input-processor-matrix rows times " \
                 "the number of gestures reported: 5 normally, 1 with diamond-tap"); \
    BUILD_ASSERT(DT_PROP_OR(ZIP_MATRIX_KSCAN_NODE(n), columns, 0) == DT_INST_PROP(n, columns), \
                 "zmk,kscan-input-matrix columns must equal zmk,input-processor-matrix columns"); \
    ZIP_MATRIX_VALIDATE_DIAMOND_TAP(n)

#if ZIP_MATRIX_USE_DIAMOND_TAP
/*
 * A diamond instance reports taps only, so it never uses the four flick rows and
 * the kscan proxy behind it needs just one row per grid row.
 */
#define ZIP_MATRIX_GESTURE_ROWS(n) \
    (DT_INST_PROP(n, diamond_tap) ? 1 : ZIP_MATRIX_GESTURE_COUNT)
#define ZIP_MATRIX_VALIDATE_DIAMOND_TAP(n) \
    BUILD_ASSERT(!DT_INST_PROP(n, diamond_tap) || \
                     (DT_INST_PROP(n, rows) == 1 && DT_INST_PROP(n, columns) == 4), \
                 "diamond-tap requires rows = 1 and columns = 4");
#define ZIP_MATRIX_DIAMOND_TAP_FIELD(n) \
        .diamond_tap = DT_INST_PROP(n, diamond_tap),
#else
#define ZIP_MATRIX_GESTURE_ROWS(n) ZIP_MATRIX_GESTURE_COUNT
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

/*
 * Release a reported hold that can no longer be released by the normal path.
 *
 * A hold is pressed from the delayed work and released from handle_event, when
 * the touch ends. But which processors run is decided per event from the layer
 * active at that moment: once the layer changes, this processor stops being
 * called and the BTN_TOUCH release is routed elsewhere, so that release never
 * happens and the kscan cell stays pressed indefinitely.
 *
 * Layer changes are therefore watched directly. A hold this instance has
 * already reported is released, and a hold that is merely pending is cancelled
 * - the delayed work would otherwise fire after the layer had already changed
 * and press a cell that nothing can then release.
 */
static void zip_matrix_release_hold_on_layer_change(const struct device *dev)
{
    struct zip_matrix_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool release = data->is_holding && data->hold_reported;
    uint8_t r = data->hold_row;
    uint8_t c = data->hold_column;

    data->is_holding = false;
    data->hold_reported = false;
    data->hold_release_pending = false;
    data->start_x = COORD_UNINITIALIZED;
    data->start_y = COORD_UNINITIALIZED;
    data->flick_latched = false;
    data->flick_gesture = GESTURE_TAP;
    data->flick_max_travel = 0U;
    /*
     * Invalidate the contact so a hold_work already past its spin lock cannot
     * decide it still owns this contact and report a press.
     */
    data->contact_id++;

    k_spin_unlock(&data->lock, key);

    /* Drop a pending hold whether or not one was already reported. */
    k_work_cancel_delayable(&data->hold_work);

    if (release) {
        LOG_WRN("Releasing held cell %u,%u: layer changed while it was down", r, c);
        zmk_kscan_matrix_report_event(data->kscan_dev, (uint32_t)r, (uint32_t)c, false);
    }
}

#define ZIP_MATRIX_RELEASE_HOLD(n) \
    zip_matrix_release_hold_on_layer_change(DEVICE_DT_INST_GET(n));

static int zip_matrix_layer_state_listener(const zmk_event_t *eh)
{
    ARG_UNUSED(eh);

    DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_RELEASE_HOLD)

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zip_matrix_layer, zip_matrix_layer_state_listener);
ZMK_SUBSCRIPTION(zip_matrix_layer, zmk_layer_state_changed);
