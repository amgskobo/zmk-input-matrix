/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int k_spinlock_key_t;
struct k_spinlock { int held; };
struct device { const void *config; void *data; const char *name; bool ready; };
typedef void (*kscan_callback_t)(const struct device *dev, uint32_t row, uint32_t column,
                                 bool pressed);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/* Unevaluated printf() keeps the format strings checked and the arguments used. */
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define LOG_INF(fmt, ...) LOG_DBG(fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) ((void)warnings++, LOG_DBG(fmt, ##__VA_ARGS__))
#define LOG_ERR(fmt, ...) ((void)errors++, LOG_DBG(fmt, ##__VA_ARGS__))

struct kscan_matrix_data {
    struct k_spinlock lock;
    kscan_callback_t callback;
    bool enabled;
};
struct kscan_matrix_config {
    uint32_t rows;
    uint32_t columns;
};

static int warnings;
static int errors;
static struct device proxy;
static struct device first_proxy;
static const struct device *const kscan_matrix_devices[] = {&first_proxy, &proxy};

static bool device_is_ready(const struct device *dev) { return dev->ready; }
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = 1;
    return 5;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && key == 5);
    lock->held = 0;
}
static bool kscan_matrix_device_valid(const struct device *dev);
void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row, uint32_t column,
                                   bool pressed);

/* DRIVER_FUNCTIONS */

static int calls;
static uint32_t last_row, last_column;
static bool last_pressed;

static void callback(const struct device *dev, uint32_t row, uint32_t column, bool pressed) {
    assert(dev == &proxy);
    calls++;
    last_row = row;
    last_column = column;
    last_pressed = pressed;
}

int main(void) {
    struct kscan_matrix_data data = {0};
    struct kscan_matrix_config config = {.rows = 15, .columns = 3};
    struct kscan_matrix_config empty = {.rows = 0, .columns = 3};
    struct device stranger = {.config = &config, .data = &data, .ready = true};
    proxy = (struct device){.config = &empty, .data = &data, .name = "proxy", .ready = false};

    /* A zero dimension is refused in either direction. */
    assert(kscan_matrix_init(&proxy) == -EINVAL);
    empty = (struct kscan_matrix_config){.rows = 5, .columns = 0};
    assert(kscan_matrix_init(&proxy) == -EINVAL);
    proxy.config = &config;
    assert(kscan_matrix_init(&proxy) == 0);

    /* Not ready, not ours, not configured, not enabled: nothing reaches a callback. */
    zmk_kscan_matrix_report_event(&proxy, 0, 0, true);
    proxy.ready = true;
    zmk_kscan_matrix_report_event(&stranger, 0, 0, true);
    zmk_kscan_matrix_report_event(&proxy, 0, 0, true);
    assert(kscan_matrix_configure(&proxy, NULL) == -EINVAL && errors == 1);
    assert(kscan_matrix_configure(&proxy, callback) == 0);
    zmk_kscan_matrix_report_event(&proxy, 0, 0, true);
    assert(calls == 0);

    /* Enabled: in-range cells arrive, out-of-range ones are dropped with a warning. */
    assert(kscan_matrix_enable_callback(&proxy) == 0);
    zmk_kscan_matrix_report_event(&proxy, 14, 2, true);
    assert(calls == 1 && last_row == 14 && last_column == 2 && last_pressed);
    zmk_kscan_matrix_report_event(&proxy, 15, 0, false);
    zmk_kscan_matrix_report_event(&proxy, 0, 3, false);
    assert(calls == 1 && warnings == 2);
    /* Still refused with a live callback: a device not ours, or not ready. */
    zmk_kscan_matrix_report_event(&stranger, 0, 0, true);
    proxy.ready = false;
    zmk_kscan_matrix_report_event(&proxy, 0, 0, true);
    proxy.ready = true;
    assert(calls == 1);

    /* Disabled again: silent. */
    assert(kscan_matrix_disable_callback(&proxy) == 0);
    zmk_kscan_matrix_report_event(&proxy, 1, 1, false);
    assert(calls == 1 && !data.lock.held);
    assert(kscan_matrix_device_valid(&first_proxy));
    puts("kscan proxy: PASS");
    return 0;
}
