/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZMK_INPUT_MATRIX_SUBSYSTEM "amgskobo__matrix"

enum zmk_custom_setting_value_type {
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 = 1,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
};
struct zmk_custom_setting_value {
    enum zmk_custom_setting_value_type type;
    union {
        int32_t int32_value;
        bool bool_value;
    };
};
struct zmk_custom_setting {
    const char *custom_subsystem_id;
    const char *key;
    int read_result;
    struct zmk_custom_setting_value value;
};
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;

#define ARG_UNUSED(x) ((void)(x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int errors;
#define LOG_ERR(fmt, ...) ((void)errors++, (void)sizeof(printf(fmt, ##__VA_ARGS__)))

static struct zmk_custom_setting *settings;
static size_t settings_len;
#define ZMK_CUSTOM_SETTING_FOREACH(_var)                                                           \
    for (struct zmk_custom_setting *_var = settings; _var < settings + settings_len; _var++)

static int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_value *value) {
    *value = setting->value;
    return setting->read_result;
}

/* The apply step: one devicetree instance and its six values. */
#include <zmk-input-matrix/matrix_runtime.h>
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&instance)
typedef struct { int kind; } zmk_event_t;
struct device { int unused; };
static struct device instance;
#define BOOL_SETTING(name, v)                                                                      \
    static struct zmk_custom_setting name = {                                                      \
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = (v)}}
#define INT_SETTING(name, v)                                                                       \
    static struct zmk_custom_setting name = {                                                      \
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = (v)}}
BOOL_SETTING(matrix_cs_enabled_0, true);
INT_SETTING(matrix_cs_flick_threshold_0, 120);
INT_SETTING(matrix_cs_long_press_ms_0, 300);
BOOL_SETTING(matrix_cs_suppress_abs_0, true);
BOOL_SETTING(matrix_cs_suppress_btn_touch_0, false);
BOOL_SETTING(matrix_cs_suppress_key_0, true);
static int applies;
static struct zip_matrix_runtime_params applied;

int zip_matrix_set_params(const struct device *dev,
                          const struct zip_matrix_runtime_params *params) {
    assert(dev == &instance);
    applies++;
    applied = *params;
    return 0;
}

/* DRIVER_FUNCTIONS */

static struct zmk_custom_setting int32_setting(int32_t value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = value}};
}

static struct zmk_custom_setting bool_setting(bool value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = value}};
}

static void test_readers(void) {
    bool flag = false;
    int32_t number = 7;

    struct zmk_custom_setting setting = bool_setting(true);
    assert(read_bool(&setting, &flag) && flag);
    setting.read_result = -2;
    flag = false;
    assert(!read_bool(&setting, &flag) && !flag);
    setting = int32_setting(1);
    assert(!read_bool(&setting, &flag) && !flag);

    /* Signed on purpose: the apply step range-checks the value itself. */
    setting = int32_setting(-5);
    assert(read_int32(&setting, &number) && number == -5);
    number = 7;
    setting.read_result = -2;
    assert(!read_int32(&setting, &number) && number == 7);
    setting = bool_setting(true);
    assert(!read_int32(&setting, &number) && number == 7);
}

static void test_unique_keys(void) {
    struct zmk_custom_setting list[] = {
        {.custom_subsystem_id = "other", .key = "grid.width"},
        {.custom_subsystem_id = ZMK_INPUT_MATRIX_SUBSYSTEM, .key = "grid.width"},
        {.custom_subsystem_id = "other", .key = "grid.layer"},
        {.custom_subsystem_id = ZMK_INPUT_MATRIX_SUBSYSTEM, .key = "grid.layer"},
        /* Another subsystem reusing a key after ours is not a duplicate. */
        {.custom_subsystem_id = "other", .key = "grid.layer"},
        {.custom_subsystem_id = ZMK_INPUT_MATRIX_SUBSYSTEM, .key = "grid.width"},
    };
    settings = list;

    settings_len = ARRAY_SIZE(list);
    assert(matrix_check_unique_keys() == 0);
    assert(errors == 1);

    errors = 0;
    settings_len = ARRAY_SIZE(list) - 1;
    assert(matrix_check_unique_keys() == 0);
    assert(errors == 0);
}

/* The six values are applied as one set, only when all read and fit. */
static void test_apply(void) {
    const zmk_event_t changed = {1};
    assert(matrix_settings_event_cb(&changed) == ZMK_EV_EVENT_BUBBLE);
    assert(applies == 1 && applied.enabled && applied.flick_threshold == 120);
    assert(applied.long_press_ms == 300 && applied.suppress_abs);
    assert(!applied.suppress_btn_touch && applied.suppress_key);

    struct zmk_custom_setting *values[] = {
        &matrix_cs_enabled_0, &matrix_cs_flick_threshold_0, &matrix_cs_long_press_ms_0,
        &matrix_cs_suppress_abs_0, &matrix_cs_suppress_btn_touch_0, &matrix_cs_suppress_key_0};
    for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
        values[i]->read_result = -2;
        matrix_apply_settings();
        values[i]->read_result = 0;
    }
    assert(applies == 1);

    /* Out of range: a threshold of 0 or past 16 bits, a negative or too-long
     * hold. */
    static const int32_t thresholds[] = {0, UINT16_MAX + 1};
    for (size_t i = 0; i < ARRAY_SIZE(thresholds); i++) {
        matrix_cs_flick_threshold_0.value.int32_value = thresholds[i];
        matrix_apply_settings();
    }
    matrix_cs_flick_threshold_0.value.int32_value = UINT16_MAX;
    static const int32_t holds[] = {-1, UINT16_MAX + 1};
    for (size_t i = 0; i < ARRAY_SIZE(holds); i++) {
        matrix_cs_long_press_ms_0.value.int32_value = holds[i];
        matrix_apply_settings();
    }
    assert(applies == 1);

    /* The edges of the range are accepted. */
    matrix_cs_long_press_ms_0.value.int32_value = 0;
    matrix_apply_settings();
    assert(applies == 2 && applied.flick_threshold == UINT16_MAX && applied.long_press_ms == 0);
}

int main(void) {
    assert(!matrix_namespace_handler(NULL, NULL));
    test_readers();
    test_unique_keys();
    test_apply();
    puts("matrix custom settings helpers: PASS");
    return 0;
}
