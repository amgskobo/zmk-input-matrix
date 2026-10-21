/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk-input-matrix/matrix_runtime.h>

static const struct device *const matrix = DEVICE_DT_GET(DT_NODELABEL(matrix));
static const struct device *const output = DEVICE_DT_GET(DT_NODELABEL(matrix_kscan));
struct edge {
    uint32_t row, column;
    bool pressed;
    int64_t ms;
};
static struct edge edges[32];
static size_t count;
static struct k_spinlock lock;
K_SEM_DEFINE(blocked, 0, 1);
K_SEM_DEFINE(resume, 0, 1);
K_SEM_DEFINE(drained, 0, 1);

static void record(const struct device *dev, uint32_t row, uint32_t col, bool pressed) {
    ARG_UNUSED(dev);
    k_spinlock_key_t key = k_spin_lock(&lock);
    __ASSERT_NO_MSG(count < ARRAY_SIZE(edges));
    edges[count++] = (struct edge){row, col, pressed, k_uptime_get()};
    k_spin_unlock(&lock, key);
}
static void block(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&blocked);
    __ASSERT_NO_MSG(k_sem_take(&resume, K_SECONDS(1)) == 0);
}
static void barrier(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&drained);
}
K_WORK_DEFINE(blocker, block);
K_WORK_DEFINE(fence, barrier);

static void event(uint8_t stream, uint16_t type, uint16_t code, int32_t value, bool sync) {
    struct input_event e = {.type = type, .code = code, .value = value, .sync = sync};
    struct zmk_input_processor_state s = {.input_device_index = stream};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(matrix, &e, 0, 0, &s) >= 0);
}
static void contact(uint8_t stream, int32_t x) {
    event(stream, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    event(stream, INPUT_EV_ABS, INPUT_ABS_X, x, false);
    event(stream, INPUT_EV_ABS, INPUT_ABS_Y, 100, true);
}
static void release(uint8_t stream) { event(stream, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true); }
static void expect(size_t n, uint32_t col, bool pressed) {
    __ASSERT_NO_MSG(count > n && edges[n].row == 0 && edges[n].column == col &&
                    edges[n].pressed == pressed);
}
static void clear(void) {
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = true;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    __ASSERT_NO_MSG(k_work_submit(&fence) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&drained, K_SECONDS(1)) == 0);
    for (size_t i = 0; i < count; i++) {
        printk("edge %lld %u %u %u\n", edges[i].ms, edges[i].row, edges[i].column,
               edges[i].pressed);
    }
    count = 0;
}
static void stall(void) {
    __ASSERT_NO_MSG(k_work_submit(&blocker) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&blocked, K_SECONDS(1)) == 0);
}
static void unstall(void) {
    k_sem_give(&resume);
    __ASSERT_NO_MSG(k_work_submit(&fence) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&drained, K_SECONDS(1)) == 0);
}
static void tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(device_is_ready(matrix) && device_is_ready(output));
    __ASSERT_NO_MSG(CONFIG_SYSTEM_WORKQUEUE_PRIORITY < 0);
    __ASSERT_NO_MSG(kscan_config(output, record) == 0);
    __ASSERT_NO_MSG(kscan_enable_callback(output) == 0);

    clear();
    int64_t start = k_uptime_get();
    contact(0, 100);
    contact(1, 900);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    expect(0, 0, true);
    expect(1, 1, true);
    __ASSERT_NO_MSG(edges[0].ms >= start + 20 && edges[1].ms >= start + 20);
    release(0);
    release(1);
    __ASSERT_NO_MSG(count == 4);
    expect(2, 0, false);
    expect(3, 1, false);
    printk("PASS: two independent real delayed holds release in order\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    release(0);
    unstall();
    __ASSERT_NO_MSG(count == 2);
    expect(0, 0, true);
    expect(1, 0, false);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    printk("PASS: cancel of an overdue queued hold emits one balanced tap only\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = false;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    unstall();
    k_msleep(30);
    __ASSERT_NO_MSG(count == 0);
    printk("PASS: runtime disable cancels overdue queued work without stale output\n");

    clear();
    contact(0, 100);
    contact(1, 900);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = false;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    __ASSERT_NO_MSG(count == 4);
    expect(2, 0, false);
    expect(3, 1, false);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 4);
    printk("PASS: settings disable balances both escaped hold presses before return\n");

    clear();
    contact(0, 100);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 1);
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(1, false) >= 0);
    __ASSERT_NO_MSG(count == 2);
    expect(1, 0, false);
    release(0);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1, false) >= 0);
    printk("PASS: real layer event releases the held cell exactly once\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(1, false) >= 0);
    unstall();
    k_msleep(30);
    __ASSERT_NO_MSG(count == 0);
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1, false) >= 0);
    printk("PASS: real layer event cancels overdue pending hold\n");

    for (size_t i = 0; i < count; i++) {
        printk("edge %lld %u %u %u\n", edges[i].ms, edges[i].row, edges[i].column,
               edges[i].pressed);
    }
    printk("matrix kernel boundary tests: PASS\n");
    exit(0);
}
K_THREAD_DEFINE(matrix_tests, 4096, tests, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                100);
