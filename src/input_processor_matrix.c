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
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>
#include <drivers/input_processor.h>
#include <kscan_input_matrix.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk-input-matrix/matrix_runtime.h>
#include "matrix_timeout.h"

#define COORD_UNINITIALIZED UINT16_MAX
#define ZIP_MATRIX_INIT_PRIORITY UTIL_INC(CONFIG_KSCAN_INIT_PRIORITY)
#define ZIP_MATRIX_GESTURE_COUNT 5U
#define ZIP_MATRIX_MAX_GRID_ROWS (UINT8_MAX / ZIP_MATRIX_GESTURE_COUNT)
#define ZIP_MATRIX_MAX_GRID_COLUMNS UINT8_MAX
#define ZIP_MATRIX_MAX_COORD (UINT16_MAX - 1U)
#define ZIP_MATRIX_MAX_U16 UINT16_MAX
#define ZIP_MATRIX_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#define ZIP_MATRIX_STREAM_COUNT MAX(ZIP_MATRIX_LISTENER_COUNT, 1)

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
#if ZIP_MATRIX_USE_DIAMOND_TAP
    bool diamond_tap;
#endif
    const struct device *kscan_dev;
};

/* Contact and generated-key lifecycle stay here; source output policy is downstream. */
struct zip_matrix_contact {
    uint16_t current_x, current_y;
    uint16_t start_x, start_y;
    bool is_btn_touch;
    bool is_holding, hold_reported, hold_release_pending;
    bool flick_latched;
    uint8_t flick_gesture;
    uint32_t flick_max_travel;
    bool flick_max_travel_carry;
    uint32_t contact_id, hold_contact_id;
    uint32_t hold_due_ms; /* modulo-32-bit deadline; hold_armed distinguishes zero */
    bool hold_armed;
};

struct zip_matrix_effects {
    bool cancel_hold, schedule_hold;
    bool release_hold, report_gesture;
    uint8_t gesture_row, gesture_column;
};

struct zip_matrix_data;
struct zip_matrix_stream {
    struct k_spinlock lock;
    struct zmk_kscan_matrix_claim hold_claim;
    struct zip_matrix_data *owner;
    struct k_work_delayable hold_work;
    struct zip_matrix_contact contact;
    atomic_t applied_generation;
};

struct zip_matrix_data {
    const struct zip_matrix_config *config;
    const struct device *kscan_dev;
    struct k_spinlock params_lock;
    struct zip_matrix_runtime_params params;
    atomic_t reset_generation;
    struct zip_matrix_stream streams[ZIP_MATRIX_STREAM_COUNT];
};

static struct zip_matrix_runtime_params zip_matrix_params_snapshot(struct zip_matrix_data *data)
{
    k_spinlock_key_t key = k_spin_lock(&data->params_lock);
    struct zip_matrix_runtime_params params = data->params;

    k_spin_unlock(&data->params_lock, key);
    return params;
}

static struct zip_matrix_config
zip_matrix_live_config(const struct zip_matrix_data *data,
                       const struct zip_matrix_runtime_params *params)
{
    struct zip_matrix_config config = *data->config;

    config.flick_threshold = params->flick_threshold;
    config.long_press_ms = params->long_press_ms;
    return config;
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

/* Coordinates are clamped to uint16_t before contact reduction. Each square
 * fits uint32_t; their sum needs one carry bit. Keep that bit explicitly so
 * farthest-point ordering remains exact at the largest supported resolution. */
static uint32_t travel_squared(int32_t dx, int32_t dy, bool *carry)
{
    uint32_t adx = dx < 0 ? 0U - (uint32_t)dx : (uint32_t)dx;
    uint32_t ady = dy < 0 ? 0U - (uint32_t)dy : (uint32_t)dy;
    uint32_t xx = adx * adx;
    uint32_t yy = ady * ady;
    uint32_t sum = xx + yy;
    *carry = sum < xx;
    return sum;
}

static uint32_t flick_threshold_squared(const struct zip_matrix_config *cfg)
{
    return (uint32_t)cfg->flick_threshold * cfg->flick_threshold;
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
    uint32_t adx = dx < 0 ? 0U - (uint32_t)dx : (uint32_t)dx;
    uint32_t ady = dy < 0 ? 0U - (uint32_t)dy : (uint32_t)dy;

    if (!flick_enabled(cfg)) {
        return GESTURE_TAP;
    }

    /*
     * Compare true distance rather than each axis on its own. Testing the axes
     * separately describes a square, so a diagonal travel had to be sqrt(2)
     * longer than an axis-aligned one before it counted as a flick.
     */
    bool carry;
    uint32_t travel = travel_squared(dx, dy, &carry);
    if (!carry && travel < flick_threshold_squared(cfg)) {
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

/* Caller owns the stream lock, or is initializing an unpublished stream.
 * Contact history only: button disposition and pending frames outlive resets.
 */
static void zip_matrix_clear_position(struct zip_matrix_contact *contact)
{
    contact->current_x = COORD_UNINITIALIZED;
    contact->current_y = COORD_UNINITIALIZED;
    contact->start_x = COORD_UNINITIALIZED;
    contact->start_y = COORD_UNINITIALIZED;
    contact->flick_latched = false;
    contact->flick_gesture = GESTURE_TAP;
    contact->flick_max_travel = 0U;
    contact->flick_max_travel_carry = false;
    contact->hold_due_ms = 0;
    contact->hold_armed = false;
}

/*
 * Admit the hold without a stream lock, then reconcile its contact generation.
 * A callback may reenter settings/contact handling, or another thread may end
 * the contact before admission returns. Pending release and contact identity
 * discharge that admitted claim exactly once. The output proxy serializes
 * callbacks independently; no cooperative scheduling assumption pairs edges.
 */
static void hold_work_handler(struct k_work *work)
{
    struct zip_matrix_stream *stream =
        CONTAINER_OF(work, struct zip_matrix_stream, hold_work.work);
    struct zip_matrix_data *data = stream->owner;
    atomic_val_t generation = atomic_get(&data->reset_generation);
    struct zip_matrix_runtime_params params = zip_matrix_params_snapshot(data);
    struct zip_matrix_config config = zip_matrix_live_config(data, &params);
#ifdef ZIP_MATRIX_TEST_HOLD_HOOK
    /* Owner native fixture controls this unlocked scheduling boundary. */
    extern void zip_matrix_test_hold_snapshot(void);
    zip_matrix_test_hold_snapshot();
#endif
    k_spinlock_key_t key = k_spin_lock(&stream->lock);
    bool trigger = false;
    uint8_t r = 0;
    uint8_t c = 0;
    uint32_t work_contact_id = 0;

    if (generation == atomic_get(&data->reset_generation) &&
        generation == atomic_get(&stream->applied_generation) &&
        params.enabled && params.long_press_ms > 0 &&
        stream->contact.is_btn_touch && !stream->contact.is_holding && !stream->contact.flick_latched &&
        stream->contact.start_x != COORD_UNINITIALIZED &&
        stream->contact.hold_armed &&
        (uint32_t)(k_uptime_get_32() - stream->contact.hold_due_ms) < 0x80000000U) {
        calculate_kscan_coordinates(&config, stream->contact.start_x, stream->contact.start_y, GESTURE_TAP,
                                    &r, &c);
        stream->contact.is_holding = true;
        stream->contact.hold_reported = false;
        stream->contact.hold_release_pending = false;
        stream->contact.hold_contact_id = stream->contact.contact_id;
        work_contact_id = stream->contact.hold_contact_id;
        trigger = true;
    }

    k_spin_unlock(&stream->lock, key);

    if (trigger) {
        bool admitted = zmk_kscan_matrix_acquire(data->kscan_dev, &stream->hold_claim, r, c) == 0;
        k_spinlock_key_t k2 = k_spin_lock(&stream->lock);
        bool same_hold = stream->contact.is_holding && stream->contact.hold_contact_id == work_contact_id;
        bool same_contact = stream->contact.contact_id == work_contact_id;
        bool release_after_press = !same_hold;

        if (same_hold) {
            stream->contact.hold_reported = admitted;
            release_after_press = !admitted ||
                !same_contact || stream->contact.hold_release_pending || !stream->contact.is_btn_touch;
        }

        if (same_hold && release_after_press) {
            stream->contact.is_holding = false;
            stream->contact.hold_reported = false;
            stream->contact.hold_release_pending = false;
            if (same_contact) {
                /*
                 * Still the contact that was held, so it has ended: a release
                 * is pending only once the touch is off, and a new touch would
                 * have changed the contact. Forget where it was.
                 */
                zip_matrix_clear_position(&stream->contact);
            }
        }
        k_spin_unlock(&stream->lock, k2);

        if (admitted && release_after_press) {
            (void)zmk_kscan_matrix_release(data->kscan_dev, &stream->hold_claim);
        }
    }
}

static struct zip_matrix_stream *
stream_for_event(struct zip_matrix_data *data, const struct zmk_input_processor_state *state)
{
    if (state == NULL) {
        return &data->streams[0];
    }

    if (state->input_device_index >= ZIP_MATRIX_STREAM_COUNT) {
        return NULL;
    }

    return &data->streams[state->input_device_index];
}

static void zip_matrix_resync_stream(struct zip_matrix_data *data,
                                     struct zip_matrix_stream *stream,
                                     atomic_val_t generation)
{
    k_spinlock_key_t key = k_spin_lock(&stream->lock);
    bool release = stream->contact.is_holding && stream->contact.hold_reported;

    stream->contact.is_holding = false;
    stream->contact.hold_reported = false;
    stream->contact.hold_release_pending = false;
    stream->contact.is_btn_touch = false;
    zip_matrix_clear_position(&stream->contact);
    stream->contact.contact_id++;
    atomic_set(&stream->applied_generation, generation);
    k_spin_unlock(&stream->lock, key);

    (void)k_work_cancel_delayable(&stream->hold_work);
    if (release) {
        (void)zmk_kscan_matrix_release(data->kscan_dev, &stream->hold_claim);
    }
}

/* Consume only contact state here. Do not rewrite events or dispatch output. */
static void zip_matrix_reduce_contact(struct zip_matrix_stream *data,
                                      const struct zip_matrix_config *cfg,
                                      const struct input_event *event,
                                      struct zip_matrix_effects *effects)
{
    bool is_sync = event->sync;
    switch (event->type) {
    case INPUT_EV_ABS:
        if (event->code == INPUT_ABS_X || event->code == INPUT_ABS_Y) {
            k_spinlock_key_t key = k_spin_lock(&data->lock);
            if (event->code == INPUT_ABS_X) {
                data->contact.current_x = clamp_coord_value(event->value, cfg->x);
            } else {
                data->contact.current_y = clamp_coord_value(event->value, cfg->y);
            }
            k_spin_unlock(&data->lock, key);
        }
        break;
    case INPUT_EV_KEY:
        if (event->code == INPUT_BTN_TOUCH) {
            bool on = event->value > 0;
            k_spinlock_key_t key = k_spin_lock(&data->lock);
            bool was_touch = data->contact.is_btn_touch;

            if (on && !was_touch) {
                effects->cancel_hold = true;
                effects->release_hold = data->contact.is_holding && data->contact.hold_reported;
                if (data->contact.is_holding && !data->contact.hold_reported) {
                    data->contact.hold_release_pending = true;
                } else {
                    data->contact.is_holding = false;
                    data->contact.hold_reported = false;
                    data->contact.hold_release_pending = false;
                }
                data->contact.contact_id++;
                data->contact.is_btn_touch = true;
                data->contact.flick_latched = false;
                data->contact.flick_gesture = GESTURE_TAP;
                data->contact.flick_max_travel = 0U;
                data->contact.flick_max_travel_carry = false;
                data->contact.start_x = COORD_UNINITIALIZED;
                data->contact.start_y = COORD_UNINITIALIZED;
                data->contact.hold_due_ms = 0;
                data->contact.hold_armed = false;
            } else if (!on && was_touch) {
                data->contact.is_btn_touch = false;
            }

            k_spin_unlock(&data->lock, key);
        }
        break;
    }

    if (is_sync) {
        k_spinlock_key_t key = k_spin_lock(&data->lock);

        if (data->contact.is_btn_touch) {
            if (data->contact.start_x == COORD_UNINITIALIZED && data->contact.current_x != COORD_UNINITIALIZED && data->contact.current_y != COORD_UNINITIALIZED) {
                data->contact.start_x = data->contact.current_x;
                data->contact.start_y = data->contact.current_y;
                effects->schedule_hold = cfg->long_press_ms > 0;
                data->contact.hold_due_ms = effects->schedule_hold ?
                    k_uptime_get_32() + cfg->long_press_ms : 0;
                data->contact.hold_armed = effects->schedule_hold;
            } else if (flick_enabled(cfg) && data->contact.start_x != COORD_UNINITIALIZED &&
                       !(data->contact.is_holding && data->contact.hold_contact_id == data->contact.contact_id)) {
                int32_t dx = (int32_t)data->contact.current_x - (int32_t)data->contact.start_x;
                int32_t dy = (int32_t)data->contact.current_y - (int32_t)data->contact.start_y;
                bool carry;
                uint32_t travel = travel_squared(dx, dy, &carry);

                /*
                 * Keep following the contact after it qualifies. The first
                 * sample past the threshold is the shortest vector of the whole
                 * stroke and the least trustworthy one to read a direction
                 * from, so hold on to the farthest point instead. The latch
                 * itself still fires once, to drop the pending hold.
                 */
                if ((carry || travel >= flick_threshold_squared(cfg)) &&
                    ((carry && !data->contact.flick_max_travel_carry) ||
                     (carry == data->contact.flick_max_travel_carry &&
                      travel > data->contact.flick_max_travel))) {
                    effects->cancel_hold = !data->contact.flick_latched;
                    data->contact.flick_latched = true;
                    data->contact.flick_max_travel = travel;
                    data->contact.flick_max_travel_carry = carry;
                    data->contact.flick_gesture = get_gesture_type(cfg, dx, dy);
                }
            }
        } else if (data->contact.start_x != COORD_UNINITIALIZED) {
            effects->cancel_hold = true;
            bool held = data->contact.is_holding && data->contact.hold_contact_id == data->contact.contact_id;
            bool stale_hold = data->contact.is_holding && data->contact.hold_contact_id != data->contact.contact_id;
            bool hold_reported = held && data->contact.hold_reported;
            bool flick_latched = data->contact.flick_latched;
            enum gesture_type gesture = data->contact.flick_gesture;
            uint16_t sx = data->contact.start_x, sy = data->contact.start_y, cx = data->contact.current_x, cy = data->contact.current_y;

            zip_matrix_clear_position(&data->contact);

            if (held && !hold_reported) {
                data->contact.hold_release_pending = true;
            } else if (held || !stale_hold) {
                data->contact.is_holding = false;
                data->contact.hold_reported = false;
                data->contact.hold_release_pending = false;
            }
            k_spin_unlock(&data->lock, key);

            if (held && hold_reported) {
                effects->release_hold = true;
            } else if (!held) {
                if (!flick_latched) {
                    gesture = get_gesture_type(cfg, (int32_t)cx - (int32_t)sx, (int32_t)cy - (int32_t)sy);
                }

                calculate_kscan_coordinates(cfg, sx, sy, gesture, &effects->gesture_row, &effects->gesture_column);
                effects->report_gesture = true;
            }

            return;
        } else if (data->contact.current_x != COORD_UNINITIALIZED || data->contact.current_y != COORD_UNINITIALIZED) {
            data->contact.current_x = COORD_UNINITIALIZED;
            data->contact.current_y = COORD_UNINITIALIZED;
        }

        k_spin_unlock(&data->lock, key);
    }

}

/* Claimed releases outlive reset generations. Other effects require the
 * original generation, and run outside stream locks for callback reentry. */
static bool zip_matrix_apply_effects(struct zip_matrix_data *owner,
                                     struct zip_matrix_stream *data,
                                     const struct zip_matrix_config *cfg,
                                     const struct zip_matrix_effects *effects,
                                     atomic_val_t generation_before)
{
    if (effects->release_hold) {
        (void)zmk_kscan_matrix_release(owner->kscan_dev, &data->hold_claim);
    }
    /* A release callback may change settings or layers before returning. */
    atomic_val_t generation_after = atomic_get(&owner->reset_generation);
    if (generation_after != generation_before) {
        zip_matrix_resync_stream(owner, data, generation_after);
        return false;
    }
    if (effects->cancel_hold) {
        (void)k_work_cancel_delayable(&data->hold_work);
    }
    if (effects->schedule_hold) {
        (void)k_work_reschedule(&data->hold_work,
                               K_TICKS(zip_matrix_timeout_ticks(cfg->long_press_ms,
                                                                CONFIG_SYS_CLOCK_TICKS_PER_SEC)));
    }
    if (effects->report_gesture) {
        struct zmk_kscan_matrix_claim tap = {0};
        if (zmk_kscan_matrix_acquire(owner->kscan_dev, &tap,
                                    effects->gesture_row, effects->gesture_column) == 0) {
            (void)zmk_kscan_matrix_release(owner->kscan_dev, &tap);
        }
    }
    return true;
}

/* Layer events own contact cancellation; input events only consume the
 * current activation state and preserve click pairs and report boundaries. */
static int zip_matrix_process_event(const struct device *dev, struct input_event *event,
                                    struct zmk_input_processor_state *state)
{
    struct zip_matrix_data *owner = dev->data;
    struct zip_matrix_stream *data = stream_for_event(owner, state);
    if (data == NULL) { return ZMK_INPUT_PROC_CONTINUE; }
    atomic_val_t generation = atomic_get(&owner->reset_generation);
    if (atomic_get(&data->applied_generation) != generation) {
        zip_matrix_resync_stream(owner, data, generation);
    }
    struct zip_matrix_runtime_params params = zip_matrix_params_snapshot(owner);
    struct zip_matrix_config cfg = zip_matrix_live_config(owner, &params);
    bool valid = true;
    if (params.enabled) {
        struct zip_matrix_effects effects = {0};
        zip_matrix_reduce_contact(data, &cfg, event, &effects);
        valid = zip_matrix_apply_effects(owner, data, &cfg, &effects, generation);
    }
    atomic_val_t after = atomic_get(&owner->reset_generation);
    if (after != generation) {
        zip_matrix_resync_stream(owner, data, after);
        valid = false;
    }
    /* Gesture generation fences affect only gesture effects. Original source
     * buttons, including presses/repeats and releases, reach the final owner. */
    if (!valid && event->type != INPUT_EV_KEY) {
        event->type = INPUT_EV_MSC; event->code = INPUT_MSC_SCAN; event->value = 0;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static int zip_matrix_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t p1, uint32_t p2, struct zmk_input_processor_state *state)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    return zip_matrix_process_event(dev, event, state);
}

static int zip_matrix_init(const struct device *dev)
{
    struct zip_matrix_data *data = dev->data;
    const struct zip_matrix_config *cfg = dev->config;

    if (cfg->rows == 0 || cfg->columns == 0 || cfg->x == 0 || cfg->y == 0) return -EINVAL;

    data->config = cfg;
    data->kscan_dev = cfg->kscan_dev;
    data->params = (struct zip_matrix_runtime_params){
        .enabled = true,
        .flick_threshold = cfg->flick_threshold,
        .long_press_ms = cfg->long_press_ms,
    };
    atomic_set(&data->reset_generation, 0);

    for (size_t i = 0U; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        struct zip_matrix_stream *stream = &data->streams[i];

        stream->owner = data;
        zip_matrix_clear_position(&stream->contact);
        stream->contact.is_btn_touch = false;
        stream->contact.is_holding = false;
        stream->contact.hold_reported = false;
        stream->contact.hold_release_pending = false;
        stream->contact.contact_id = 0;
        stream->contact.hold_contact_id = 0;
        atomic_set(&stream->applied_generation, 0);
        k_work_init_delayable(&stream->hold_work, hold_work_handler);
    }

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
        ZIP_MATRIX_DIAMOND_TAP_FIELD(n) \
        .kscan_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)), \
    }; \
    DEVICE_DT_INST_DEFINE(n, zip_matrix_init, NULL, &zip_matrix_data_##n, &zip_matrix_config_##n, POST_KERNEL, ZIP_MATRIX_INIT_PRIORITY, &zip_matrix_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_INST)

#define ZIP_MATRIX_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

static const struct device *const zip_matrix_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(ZIP_MATRIX_DEVICE_REF)};

static bool zip_matrix_device_valid(const struct device *dev)
{
    for (size_t i = 0U; i < ARRAY_SIZE(zip_matrix_devices); i++) {
        if (zip_matrix_devices[i] == dev) {
            return true;
        }
    }

    return false;
}

int zip_matrix_get_params(const struct device *dev, struct zip_matrix_runtime_params *out)
{
    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }
    if (!zip_matrix_device_valid(dev)) {
        return -ENODEV;
    }

    *out = zip_matrix_params_snapshot(dev->data);
    return 0;
}

int zip_matrix_set_params(const struct device *dev,
                          const struct zip_matrix_runtime_params *params)
{
    if (dev == NULL || params == NULL || params->flick_threshold == 0U) {
        return -EINVAL;
    }
    if (!zip_matrix_device_valid(dev)) {
        return -ENODEV;
    }

    struct zip_matrix_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->params_lock);
    data->params = *params;
    atomic_val_t generation = atomic_inc(&data->reset_generation) + 1;
    k_spin_unlock(&data->params_lock, key);
    for (size_t i = 0U; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        zip_matrix_resync_stream(data, &data->streams[i], generation);
    }
    return 0;
}

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
    struct zip_matrix_data *owner = dev->data;
    atomic_val_t generation = atomic_inc(&owner->reset_generation) + 1;

    for (size_t i = 0U; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        struct zip_matrix_stream *stream = &owner->streams[i];
        zip_matrix_resync_stream(owner, stream, generation);
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
