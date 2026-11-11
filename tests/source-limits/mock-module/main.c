/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>

static atomic_t observed;
static void record(const struct device *dev, uint32_t row, uint32_t col, bool down) {
    ARG_UNUSED(dev);
    __ASSERT_NO_MSG(row == 0 && col == 0 && down);
    atomic_inc(&observed);
}
static void activate(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const struct device *mock = DEVICE_DT_GET(DT_NODELABEL(kscan));
    __ASSERT_NO_MSG(device_is_ready(mock));
    __ASSERT_NO_MSG(kscan_config(mock, record) == 0);
    __ASSERT_NO_MSG(kscan_enable_callback(mock) == 0);
    k_msleep(5);
    __ASSERT_NO_MSG(atomic_get(&observed) == 1);
    printk("matrix active nonempty mock decoder: PASS\n");
}
K_THREAD_DEFINE(matrix_live_mock, 4096, activate, NULL, NULL, NULL, 8, 0, 10);
