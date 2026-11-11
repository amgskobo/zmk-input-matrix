/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#include <string.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>
#include <zmk-input-matrix/matrix_runtime.h>

#define SOURCE_ID_LEFT(n, compat, unused) \
    + (DT_SAME_NODE(DT_INST(n, compat), DT_PATH(matrix_listener_left)) ? (n) : 0)
#define SOURCE_ID_RIGHT(n, compat, unused) \
    + (DT_SAME_NODE(DT_INST(n, compat), DT_PATH(matrix_listener_right)) ? (n) : 0)
enum {
    LEFT_ID = 0 DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(zmk_input_listener, SOURCE_ID_LEFT, unused),
    RIGHT_ID = 0 DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(zmk_input_listener, SOURCE_ID_RIGHT, unused),
};
BUILD_ASSERT(LEFT_ID == CONFIG_MATRIX_SOURCE_TEST_LEFT, "actual left listener ID differs");
BUILD_ASSERT(RIGHT_ID == CONFIG_MATRIX_SOURCE_TEST_RIGHT, "actual right listener ID differs");
static const struct device *const matrix = DEVICE_DT_GET(DT_NODELABEL(matrix));
static const struct device *const output = DEVICE_DT_GET(DT_NODELABEL(matrix_kscan));
static bool held[2];
static unsigned edges;
static void record(const struct device *dev, uint32_t row, uint32_t col, bool down) {
    ARG_UNUSED(dev);
    __ASSERT_NO_MSG(row == 0 && col < 2 && held[col] != down);
    held[col] = down;
    printk("MATRIX SOURCE EDGE index=%u column=%u down=%u ms=%lld\n", edges++, col, down, k_uptime_get());
}
static void report(const struct device *source, bool down, int x) {
    if (down) {
        __ASSERT_NO_MSG(input_report_key(source, INPUT_BTN_TOUCH, 1, false, K_FOREVER) == 0);
        __ASSERT_NO_MSG(input_report_abs(source, INPUT_ABS_X, x, false, K_FOREVER) == 0);
        __ASSERT_NO_MSG(input_report_abs(source, INPUT_ABS_Y, 100, true, K_FOREVER) == 0);
    } else {
        __ASSERT_NO_MSG(input_report_key(source, INPUT_BTN_TOUCH, 0, true, K_FOREVER) == 0);
    }
    k_msleep(35);
}
static void tests(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    __ASSERT_NO_MSG(device_is_ready(matrix) && device_is_ready(output));
    __ASSERT_NO_MSG(kscan_config(output, record) == 0 && kscan_enable_callback(output) == 0);
    const struct device *left = DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
    const struct device *right = DEVICE_DT_GET(DT_NODELABEL(matrix_source_right));
    report(left, true, 100); report(right, true, 900);
    __ASSERT_NO_MSG(held[0] && held[1] && edges == 2);
    report(left, false, 0);
    __ASSERT_NO_MSG(!held[0] && held[1] && edges == 3);
    struct zip_matrix_runtime_params params;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &params) == 0);
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &params) == 0);
    __ASSERT_NO_MSG(!held[0] && !held[1] && edges == 4);
    report(right, false, 0);
    __ASSERT_NO_MSG(edges == 4);
    /* Direct callers keep NULL=0 and excluded-index pass-through semantics. */
    struct input_event e = {.type = INPUT_EV_KEY, .code = INPUT_BTN_TOUCH, .value = 1};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(matrix, &e, 0, 0, NULL) == 0);
    const struct input_event original = e;
    struct zmk_input_processor_state invalid = {.input_device_index = 255};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(matrix, &e, 0, 0, &invalid) == 0);
    __ASSERT_NO_MSG(memcmp(&e, &original, sizeof(e)) == 0 && edges == 4);
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &params) == 0);
    printk("matrix actual listener allocation: PASS left=%u right=%u bound=%u\n",
           LEFT_ID, RIGHT_ID, CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES);
    exit(0);
}
K_THREAD_DEFINE(matrix_source_tests, 4096, tests, NULL, NULL, NULL, 8, 0, 100);
