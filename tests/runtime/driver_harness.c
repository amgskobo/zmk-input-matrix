/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-input-matrix/matrix_runtime.h>

typedef long atomic_t;
typedef long atomic_val_t;
typedef int k_spinlock_key_t;
typedef struct { int64_t ms; } k_timeout_t;
struct k_spinlock { int held; };
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t sync; uint16_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { uint8_t input_device_index; };

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ARG_UNUSED(x) ((void)(x))
#define CONTAINER_OF(ptr, type, field) ((type *)(void *)((char *)(ptr) - offsetof(type, field)))
/* Unevaluated printf() keeps the format strings checked and the arguments used. */
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_EV_ABS 0x03
#define INPUT_ABS_X 0x00
#define INPUT_ABS_Y 0x01
#define INPUT_ABS_PRESSURE 0x18
#define INPUT_BTN_0 0x100
#define INPUT_BTN_TOUCH 0x14a
#define INPUT_KEY_A 30
#define ZMK_INPUT_PROC_CONTINUE 0
#define ZMK_INPUT_PROC_STOP 1

#define COORD_UNINITIALIZED UINT16_MAX
#define COORD_INVALID_ZERO 0xFFF
#define ZIP_MATRIX_STREAM_COUNT 2
#define ZIP_MATRIX_USE_DIAMOND_TAP 1
#define ZIP_MATRIX_TRACKED_BTNS 16
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&matrix)
typedef struct { int kind; } zmk_event_t;

enum gesture_type { GESTURE_TAP, GESTURE_UP, GESTURE_DOWN, GESTURE_LEFT, GESTURE_RIGHT };

struct zip_matrix_config {
    uint8_t rows;
    uint8_t columns;
    uint16_t x;
    uint16_t y;
    uint16_t flick_threshold;
    uint16_t long_press_ms;
    bool suppress_abs;
    bool suppress_btn_touch;
    bool suppress_key;
    bool diamond_tap;
    const struct device *kscan_dev;
};
struct zip_matrix_data;
struct zip_matrix_stream {
    struct k_spinlock lock;
    struct zip_matrix_data *owner;
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
    uint64_t flick_max_travel;
    uint8_t hold_row;
    uint8_t hold_column;
    uint32_t contact_id;
    uint32_t hold_contact_id;
    uint16_t suppressed_btns;
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

static struct device matrix;
static struct device kscan = {.name = "kscan"};
static struct device first_matrix;
static const struct device *const zip_matrix_devices[] = {&first_matrix, &matrix};

/* Every kscan report, in order. */
struct report { uint8_t row, column; bool pressed; };
static struct report reports[64];
static size_t report_count;
/* Runs inside a kscan report, where the hold worker leaves its gap. */
static void (*during_report)(void);
/* Counts atomic_get() calls down; at zero a settings write lands on that read. */
static int writer_lands_on_read;

static atomic_val_t atomic_get(const atomic_t *value) {
    if (writer_lands_on_read > 0 && --writer_lands_on_read == 0) {
        (*(atomic_t *)value)++;
    }
    return *value;
}
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static atomic_val_t atomic_inc(atomic_t *value) { return (*value)++; }
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = 1;
    return 3;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && key == 3);
    lock->held = 0;
}
static void k_work_init_delayable(struct k_work_delayable *work,
                                  void (*handler)(struct k_work *work)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
}
static int k_work_reschedule(struct k_work_delayable *work, k_timeout_t delay) {
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}
static void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row,
                                          uint32_t column, bool pressed) {
    assert(dev == &kscan && report_count < ARRAY_SIZE(reports));
    reports[report_count++] = (struct report){(uint8_t)row, (uint8_t)column, pressed};
    if (during_report != NULL) {
        void (*hook)(void) = during_report;
        during_report = NULL;
        hook();
    }
}

/* DRIVER_FUNCTIONS */

static struct zip_matrix_data data;
static struct zip_matrix_config config;
static struct zmk_input_processor_state second = {.input_device_index = 1};

static struct zip_matrix_stream *stream(size_t index) { return &data.streams[index]; }

static void expect_no_hold(size_t index) {
    assert(!stream(index)->is_holding && !stream(index)->hold_reported);
    assert(!stream(index)->hold_release_pending);
}

static void expect_positions_forgotten(size_t index) {
    assert(stream(index)->start_x == COORD_UNINITIALIZED &&
           stream(index)->start_y == COORD_UNINITIALIZED);
    assert(stream(index)->current_x == COORD_UNINITIALIZED &&
           stream(index)->current_y == COORD_UNINITIALIZED);
}

static void expect_no_flick(size_t index) {
    assert(!stream(index)->flick_latched && stream(index)->flick_gesture == GESTURE_TAP);
    assert(stream(index)->flick_max_travel == 0U);
}

static int send_to(struct zmk_input_processor_state *state, uint16_t type, uint16_t code,
                   int32_t value, bool sync) {
    struct input_event event = {.sync = sync, .type = type, .code = code, .value = value};
    const int ret = zip_matrix_handle_event(&matrix, &event, 0, 0, state);
    if (ret == ZMK_INPUT_PROC_STOP) {
        assert(event.code == COORD_INVALID_ZERO && !event.sync);
    } else {
        assert(event.code == code && event.sync == sync);
    }
    return ret;
}

static int send(uint16_t type, uint16_t code, int32_t value, bool sync) {
    return send_to(NULL, type, code, value, sync);
}

static void frame(int32_t x, int32_t y) {
    send(INPUT_EV_ABS, INPUT_ABS_X, x, false);
    send(INPUT_EV_ABS, INPUT_ABS_Y, y, true);
}

static void press(void) { send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false); }
static void release_touch(void) { send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false); }
/* A release and the frame that ends it: the sync that reports the gesture. */
static void lift(void) {
    release_touch();
    send(INPUT_EV_ABS, INPUT_ABS_PRESSURE, 0, true);
}

static void expect(size_t index, uint8_t row, uint8_t column, bool pressed) {
    if (index >= report_count || reports[index].row != row ||
        reports[index].column != column || reports[index].pressed != pressed) {
        fprintf(stderr, "report %zu of %zu: want %u,%u,%d", index, report_count, row, column,
                pressed);
        if (index < report_count) {
            fprintf(stderr, " got %u,%u,%d", reports[index].row, reports[index].column,
                    reports[index].pressed);
        }
        fputc('\n', stderr);
        assert(false);
    }
}

static void run_hold(size_t index) {
    stream(index)->hold_work.scheduled = false;
    stream(index)->hold_work.work.handler(&stream(index)->hold_work.work);
}

static struct zip_matrix_runtime_params params_of(bool enabled, uint16_t threshold,
                                                  uint16_t long_press) {
    return (struct zip_matrix_runtime_params){.enabled = enabled, .flick_threshold = threshold,
                                              .long_press_ms = long_press,
                                              .suppress_abs = true,
                                              .suppress_btn_touch = true,
                                              .suppress_key = false};
}

static void test_api(void) {
    struct device stranger = {.config = &config, .data = &data};
    struct zip_matrix_runtime_params out;
    struct zip_matrix_config zero = config;

    zero.columns = 0;
    matrix.config = &zero;
    assert(zip_matrix_init(&matrix) == -EINVAL);
    zero = config;
    zero.x = 0;
    assert(zip_matrix_init(&matrix) == -EINVAL);
    zero = config;
    zero.y = 0;
    assert(zip_matrix_init(&matrix) == -EINVAL);
    zero = config;
    zero.rows = 0;
    assert(zip_matrix_init(&matrix) == -EINVAL);
    matrix.config = &config;
    for (size_t i = 0; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        stream(i)->is_btn_touch = stream(i)->is_holding = stream(i)->hold_reported = true;
        stream(i)->hold_release_pending = stream(i)->flick_latched = true;
        stream(i)->flick_gesture = GESTURE_UP;
        stream(i)->flick_max_travel = 9;
        stream(i)->start_x = stream(i)->start_y = 5;
        stream(i)->current_x = stream(i)->current_y = 5;
        stream(i)->contact_id = stream(i)->hold_contact_id = 7;
        stream(i)->suppressed_btns = 3;
        stream(i)->applied_generation = 4;
    }
    data.reset_generation = 4;
    assert(zip_matrix_init(&matrix) == 0);
    assert(data.reset_generation == 0);
    for (size_t i = 0; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        expect_no_hold(i);
        expect_no_flick(i);
        expect_positions_forgotten(i);
        assert(!stream(i)->is_btn_touch && stream(i)->suppressed_btns == 0);
        assert(stream(i)->contact_id == 0 && stream(i)->hold_contact_id == 0);
        assert(stream(i)->applied_generation == 0);
    }
    assert(data.kscan_dev == &kscan && stream(1)->owner == &data);

    assert(zip_matrix_get_params(NULL, &out) == -EINVAL);
    assert(zip_matrix_get_params(&matrix, NULL) == -EINVAL);
    assert(zip_matrix_get_params(&stranger, &out) == -ENODEV);
    assert(zip_matrix_get_params(&matrix, &out) == 0);
    assert(out.enabled && out.flick_threshold == 100 && out.long_press_ms == 300);

    const struct zip_matrix_runtime_params valid = params_of(true, 100, 300);
    const struct zip_matrix_runtime_params no_threshold = params_of(true, 0, 300);
    assert(zip_matrix_set_params(NULL, &valid) == -EINVAL);
    assert(zip_matrix_set_params(&matrix, NULL) == -EINVAL);
    assert(zip_matrix_set_params(&matrix, &no_threshold) == -EINVAL);
    assert(zip_matrix_set_params(&stranger, &valid) == -ENODEV);
    assert(zip_matrix_set_params(&matrix, &valid) == 0);
    assert(data.reset_generation == 1 && stream(1)->applied_generation == 1);
}

static void test_taps_and_flicks(void) {
    /* 3x3 grid on 900x900: a tap reports its cell on the tap rows. */
    const uint32_t contact = stream(0)->contact_id;
    press();
    assert(stream(0)->contact_id == contact + 1);
    expect_no_hold(0);
    frame(450, 150);
    assert(stream(0)->start_x == 450 && stream(0)->hold_work.scheduled);
    send(INPUT_EV_ABS, INPUT_ABS_X, 460, false);
    assert(stream(0)->hold_work.scheduled);
    send(INPUT_EV_ABS, INPUT_ABS_X, 450, false);
    lift();
    assert(report_count == 2);
    expect(0, 0, 1, true);
    expect(1, 0, 1, false);
    assert(!stream(0)->hold_work.scheduled);
    expect_positions_forgotten(0);

    /* A flick keeps the farthest point and reports once, from the start cell. */
    press();
    frame(450, 450);
    frame(450, 500);  /* below the threshold */
    assert(!stream(0)->flick_latched);
    frame(550, 450);  /* right, exactly the threshold: latches */
    assert(stream(0)->flick_latched && stream(0)->flick_max_travel == 10000U);
    assert(!stream(0)->hold_work.scheduled);
    frame(600, 450);  /* farther right */
    assert(stream(0)->flick_latched && !stream(0)->hold_work.scheduled);
    frame(560, 450);  /* shorter: ignored */
    frame(450, 250);  /* longer and upward: the direction follows */
    frame(450, 250);  /* no farther */
    frame(650, 450);  /* as far, but right: the first stays */
    assert(stream(0)->flick_gesture == GESTURE_UP);
    lift();
    expect_no_flick(0);
    expect(2, 1 * 3 + 1, 1, true); /* GESTURE_UP row block 1, middle cell */
    expect(3, 1 * 3 + 1, 1, false);

    /* Travel measured at release when nothing latched. */
    press();
    frame(450, 450);
    send(INPUT_EV_ABS, INPUT_ABS_X, 700, false);
    lift();
    expect(4, 4 * 3 + 1, 1, true); /* GESTURE_RIGHT from the middle cell */
}

static void test_frames_without_touch(void) {
    /* Coordinates with no touch are forgotten at the frame end. */
    frame(10, 10);
    assert(stream(0)->current_x == COORD_UNINITIALIZED);
    send(INPUT_EV_ABS, INPUT_ABS_Y, 20, true);
    assert(stream(0)->current_y == COORD_UNINITIALIZED);
    send(INPUT_EV_ABS, INPUT_ABS_X, 20, true);
    assert(stream(0)->current_x == COORD_UNINITIALIZED);
    send(INPUT_EV_ABS, INPUT_ABS_PRESSURE, 20, true);

    /* A touch whose first frame lacks an axis waits for both. */
    press();
    send(INPUT_EV_ABS, INPUT_ABS_PRESSURE, 5, true);
    send(INPUT_EV_ABS, INPUT_ABS_X, 100, true);
    assert(stream(0)->start_x == COORD_UNINITIALIZED);
    send(INPUT_EV_ABS, INPUT_ABS_Y, 100, true);
    assert(stream(0)->start_x == 100);
    /* A second press edge while touching changes nothing. */
    const uint32_t contact = stream(0)->contact_id;
    press();
    assert(stream(0)->contact_id == contact);
    lift();
    /* A release edge with no touch open changes nothing. */
    release_touch();
    assert(!stream(0)->is_btn_touch);
    report_count = 0;
}

static void release_with_flick_state(void) {
    release_touch();
    stream(0)->flick_latched = true;
    stream(0)->flick_gesture = GESTURE_UP;
    stream(0)->flick_max_travel = 5;
}

static void test_long_press(void) {
    /* The hold presses the tap cell and the lift releases it. */
    press();
    frame(100, 800);
    run_hold(0);
    assert(report_count == 1 && stream(0)->is_holding && stream(0)->hold_reported);
    expect(0, 2, 0, true);
    frame(700, 800); /* holding: no flick */
    assert(!stream(0)->flick_latched);
    lift();
    expect(1, 2, 0, false);
    assert(report_count == 2);
    expect_no_hold(0);

    /* The worker does nothing once the contact has flicked, ended or started
     * holding, or before it has a start point. */
    press();
    run_hold(0); /* no start yet */
    frame(100, 100);
    frame(400, 100);
    run_hold(0); /* flicked */
    lift();
    run_hold(0); /* ended */
    press();
    frame(100, 100);
    run_hold(0);
    run_hold(0); /* already holding */
    assert(report_count == 5);
    lift();
    press();
    frame(100, 100);
    data.params.enabled = false; /* switched off, not yet resynced */
    run_hold(0);
    data.params.enabled = true;
    assert(report_count == 6 && !stream(0)->is_holding);
    lift();
    report_count = 0;

    /* A touch that ends while the hold is being pressed is released by the
     * worker itself, and its position forgotten. */
    press();
    frame(100, 100);
    during_report = release_with_flick_state;
    run_hold(0);
    assert(report_count == 2 && !reports[1].pressed);
    expect_no_hold(0);
    expect_no_flick(0);
    expect_positions_forgotten(0);
    assert(!stream(0)->is_holding && stream(0)->current_x == COORD_UNINITIALIZED);

    /* ...likewise when the lift's frame lands in the gap. */
    press();
    frame(100, 100);
    during_report = lift;
    run_hold(0);
    assert(report_count == 4 && !reports[3].pressed);
    expect_no_hold(0);

    /* A new contact landing in the gap: the old hold is released, the new
     * contact keeps its state. */
    press();
    frame(100, 100);
    during_report = press;
    run_hold(0);
    /* press() while touching changes nothing, so end and restart the touch. */
    assert(report_count == 5 && stream(0)->hold_reported);
    lift();
    report_count = 0;
    press();
    frame(100, 100);
    during_report = NULL;
    stream(0)->is_btn_touch = true;
    const uint32_t first = stream(0)->contact_id;
    run_hold(0);
    assert(stream(0)->hold_contact_id == first);
    lift();
    report_count = 0;
}

static void new_contact_in_gap(void) {
    release_touch();
    press();
}

/* A different hold took the stream's place while this one was pressed. */
static void other_hold_in_gap(void) { stream(0)->hold_contact_id++; }

static void resync_in_gap(void) {
    const struct zip_matrix_runtime_params p = params_of(true, 100, 300);
    assert(zip_matrix_set_params(&matrix, &p) == 0);
}

static void test_hold_cells(void) {
    /* 3x3 on 900x900: 800,800 is row 2, column 2 of the tap rows. */
    press();
    frame(800, 800);
    stream(0)->hold_reported = true;
    stream(0)->hold_release_pending = true;
    run_hold(0);
    assert(report_count == 1 && stream(0)->hold_reported && !stream(0)->hold_release_pending);
    expect(0, 2, 2, true);
    lift();
    expect(1, 2, 2, false);
    press();
    frame(800, 800);
    run_hold(0);
    expect(2, 2, 2, true);
    release_touch();
    press(); /* a new contact over the reported hold releases it */
    expect(3, 2, 2, false);
    lift();
    report_count = 0;

    /* A press edge after a release whose sync never came starts clean. */
    press();
    frame(100, 100);
    frame(400, 100);
    assert(stream(0)->flick_latched);
    release_touch();
    press();
    assert(stream(0)->start_x == COORD_UNINITIALIZED && stream(0)->start_y == COORD_UNINITIALIZED);
    expect_no_flick(0);
    lift();
    report_count = 0;
}

static void test_hold_races(void) {
    /* The contact changes while the hold is pressed: released at once, and the
     * new contact's start is left alone. */
    press();
    frame(100, 100);
    during_report = new_contact_in_gap;
    run_hold(0);
    assert(report_count == 2 && !reports[1].pressed);
    assert(stream(0)->is_btn_touch && !stream(0)->is_holding);
    lift();
    report_count = 0;

    /* A settings write lands while the hold is pressed. The resync clears the
     * hold as unreported, so the worker releases what it pressed. */
    press();
    frame(100, 100);
    const uint32_t before_resync = stream(0)->contact_id;
    during_report = resync_in_gap;
    run_hold(0);
    assert(report_count == 2 && !reports[1].pressed);
    expect_no_hold(0);
    assert(stream(0)->contact_id == before_resync + 1);
    report_count = 0;

    /* The stream holds for another contact by the time this press is out:
     * this one is released, and that one left alone. */
    press();
    frame(100, 100);
    during_report = other_hold_in_gap;
    run_hold(0);
    assert(report_count == 2 && !reports[1].pressed && stream(0)->is_holding);
    assert(!stream(0)->hold_reported);
    stream(0)->is_holding = false;
    lift();
    report_count = 0;

    /* A press edge over a reported hold releases it; over an unreported one it
     * leaves the release to the worker. */
    press();
    frame(100, 100);
    run_hold(0);
    release_touch();
    press();
    assert(report_count == 2 && !reports[1].pressed);
    expect_no_hold(0);
    lift();
    report_count = 0;
    press();
    frame(100, 100);
    release_touch();
    assert(stream(0)->hold_work.scheduled);
    press();
    assert(!stream(0)->hold_work.scheduled);
    lift();
    report_count = 0;
    stream(0)->is_holding = true;
    stream(0)->hold_reported = false;
    press();
    assert(stream(0)->hold_release_pending);
    lift();
    report_count = 0;

    /* The lift of a contact whose hold is still being pressed marks it for the
     * worker. */
    press();
    frame(100, 100);
    stream(0)->is_holding = true;
    stream(0)->hold_reported = false;
    stream(0)->hold_release_pending = false;
    stream(0)->hold_contact_id = stream(0)->contact_id;
    lift();
    assert(stream(0)->is_holding && stream(0)->hold_release_pending && report_count == 0);

    /* A lift over an older contact's unfinished hold reports the new tap and
     * leaves the old hold to its worker. */
    press();
    frame(100, 100);
    stream(0)->is_holding = true;
    stream(0)->hold_contact_id = stream(0)->contact_id - 1;
    frame(400, 100); /* moving, the stale hold does not stop a flick */
    assert(stream(0)->flick_latched);
    lift();
    assert(stream(0)->is_holding && report_count == 2);
    report_count = 0;
    resync_in_gap();
}

static void test_suppression(void) {
    /* suppress_key drops button pairs it saw pressed, passes other releases. */
    struct zip_matrix_runtime_params p = params_of(true, 100, 0);
    p.suppress_abs = false;
    p.suppress_btn_touch = false;
    p.suppress_key = true;
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0, 1, true) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0 + 16, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0 + 16, 0, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0 + 15, 0, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_KEY, INPUT_KEY_A, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_ABS, INPUT_ABS_X, 5, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false) == ZMK_INPUT_PROC_STOP);

    /* suppress_btn_touch alone drops only the touch. */
    p.suppress_key = false;
    p.suppress_btn_touch = true;
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false) == ZMK_INPUT_PROC_STOP);

    /* Neither: everything passes; other event types always pass. */
    p.suppress_btn_touch = false;
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(INPUT_EV_REL, 0, 3, false) == ZMK_INPUT_PROC_CONTINUE);

    /* Disabled: nothing is read or dropped. */
    p.enabled = false;
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    assert(send(INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(stream(0)->current_x == COORD_UNINITIALIZED);
    report_count = 0;
    resync_in_gap();
}

static void test_streams_and_generations(void) {
    /* Past the listeners: untouched. */
    struct zmk_input_processor_state stranger = {.input_device_index = ZIP_MATRIX_STREAM_COUNT};
    assert(send_to(&stranger, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false) == ZMK_INPUT_PROC_CONTINUE);

    /* A second listener keeps its own contact. */
    send_to(&second, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    assert(stream(1)->is_btn_touch && !stream(0)->is_btn_touch);
    send_to(&second, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);

    /* A stream that missed a reset catches up before its next event. */
    press();
    frame(100, 100);
    frame(400, 100);
    assert(stream(0)->flick_latched);
    stream(0)->suppressed_btns = 3;
    stream(0)->hold_release_pending = true;
    data.reset_generation++;
    send(INPUT_EV_ABS, INPUT_ABS_X, 100, false);
    assert(!stream(0)->is_btn_touch && stream(0)->start_x == COORD_UNINITIALIZED);
    expect_no_flick(0);
    expect_no_hold(0);
    assert(stream(0)->suppressed_btns == 0 && stream(0)->start_y == COORD_UNINITIALIZED);
    assert(stream(0)->current_y == COORD_UNINITIALIZED);

    /* A reset landing mid-event throws the event away. */
    press();
    frame(100, 100);
    writer_lands_on_read = 3;
    assert(send(INPUT_EV_ABS, INPUT_ABS_X, 100, true) == ZMK_INPUT_PROC_STOP);
    assert(!stream(0)->is_btn_touch);

    /* Likewise an event that would have passed untouched. */
    writer_lands_on_read = 3;
    assert(send(INPUT_EV_REL, 0, 3, true) == ZMK_INPUT_PROC_STOP);

    /* A layer change releases a reported hold and cancels a pending one. */
    press();
    frame(100, 100);
    run_hold(0);
    assert(stream(0)->hold_reported);
    zip_matrix_release_hold_on_layer_change(&matrix);
    assert(report_count == 2 && !reports[1].pressed);
    expect_no_hold(0);
    for (size_t i = 0; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        assert(stream(i)->applied_generation == data.reset_generation);
    }
    /* ...reached through the layer listener, for every instance. */
    press();
    frame(100, 100);
    assert(stream(0)->hold_work.scheduled);
    const zmk_event_t layer_changed = {1};
    assert(zip_matrix_layer_state_listener(&layer_changed) == 0);
    assert(!stream(0)->hold_work.scheduled && report_count == 2);
    report_count = 0;
}

static void test_diamond(void) {
    /* A 1x4 diamond reports taps only, chosen by where the finger rests. */
    config.rows = 1;
    config.columns = 4;
    config.diamond_tap = true;
    press();
    frame(450, 100);
    frame(800, 100); /* travel alone never flicks */
    assert(!stream(0)->flick_latched);
    lift();
    expect(0, 0, 0, true); /* up */
    press();
    frame(850, 450);
    send(INPUT_EV_ABS, INPUT_ABS_Y, 450, false);
    release_touch();
    send(INPUT_EV_ABS, INPUT_ABS_Y, 450, true);
    expect(2, 0, 1, true); /* right */
    config.rows = 3;
    config.columns = 3;
    config.diamond_tap = false;
    report_count = 0;
}

static void test_runtime_params(void) {
    struct zip_matrix_runtime_params p = params_of(true, 300, 1);
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    press();
    frame(450, 450);
    assert(stream(0)->hold_work.scheduled && stream(0)->hold_work.delay_ms == 1);
    frame(650, 450); /* 200: a flick at 100, not at 300 */
    assert(!stream(0)->flick_latched);
    frame(800, 450);
    assert(stream(0)->flick_latched);
    lift();
    report_count = 0;
    p = params_of(true, 100, 300);
    assert(zip_matrix_set_params(&matrix, &p) == 0);
}

static void test_no_long_press(void) {
    const struct zip_matrix_runtime_params p = params_of(true, 100, 0);
    assert(zip_matrix_set_params(&matrix, &p) == 0);
    press();
    frame(100, 100);
    assert(!stream(0)->hold_work.scheduled);
    lift();
    report_count = 0;
}

int main(void) {
    config = (struct zip_matrix_config){.rows = 3, .columns = 3, .x = 900, .y = 900,
                                        .flick_threshold = 100, .long_press_ms = 300,
                                        .suppress_abs = true, .suppress_btn_touch = true,
                                        .kscan_dev = &kscan};
    matrix = (struct device){.config = &config, .data = &data, .name = "matrix"};

    test_api();
    test_taps_and_flicks();
    report_count = 0;
    test_frames_without_touch();
    test_long_press();
    test_hold_cells();
    test_hold_races();
    test_suppression();
    test_streams_and_generations();
    test_diamond();
    test_runtime_params();
    test_no_long_press();

    for (size_t i = 0; i < ZIP_MATRIX_STREAM_COUNT; i++) {
        assert(!stream(i)->lock.held);
    }
    assert(!data.params_lock.held);
    assert(zip_matrix_device_valid(&first_matrix));
    puts("matrix driver: PASS");
    return 0;
}
