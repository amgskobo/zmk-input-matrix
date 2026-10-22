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

#ifndef CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE
#define CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE 8
#endif
#define CONFIG_ZMK_INPUT_MATRIX_MAX_CLAIMS 4
struct zmk_kscan_matrix_claim {
    struct zmk_kscan_matrix_claim *next;
    uint32_t row, column;
};
/* DRIVER_TYPES */

static int warnings;
static int errors;
static struct device proxy;
static struct device first_proxy;
static const struct device *const kscan_matrix_devices[] = {&first_proxy, &proxy};

static bool device_is_ready(const struct device *dev) { return dev->ready; }
static volatile uint64_t lock_calls;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock_calls++;
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

static bool check_queue_order;
static unsigned queue_ordinal;
static void (*during_callback)(void);
static void callback(const struct device *dev, uint32_t row, uint32_t column, bool pressed) {
    assert(dev == &proxy && !((struct kscan_matrix_data *)dev->data)->lock.held);
    if (check_queue_order) {
        unsigned i = queue_ordinal++;
        unsigned pairs = (CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE - 1U) / 2U;
        unsigned cell = (i == 0 || i == 2U * pairs + 1U) ? 0 : (i + 1U) / 2U;
        assert(row == cell % 15U && column == cell / 15U);
        assert(pressed == (i == 0 || (i < 2U * pairs + 1U && (i & 1U))));
    }
    calls++;
    last_row = row;
    last_column = column;
    last_pressed = pressed;
    if (during_callback) {
        void (*hook)(void) = during_callback;
        during_callback = NULL;
        hook();
    }
}

static struct zmk_kscan_matrix_claim reentrant[4];
static void fill_reentrant_queue(void) {
    /* The first press has left the queue, but its final release is reserved. */
    unsigned pairs = (CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE - 1U) / 2U;
    for (unsigned cell = 1; cell <= pairs; cell++) {
        assert(zmk_kscan_matrix_acquire(&proxy, &reentrant[1], cell % 15U, cell / 15U) == 0);
        assert(zmk_kscan_matrix_release(&proxy, &reentrant[1]) == 0);
    }
    assert(zmk_kscan_matrix_acquire(&proxy, &reentrant[2], 14, 2) == -ENOSPC);
    assert(zmk_kscan_matrix_release(&proxy, &reentrant[2]) == -EINVAL);
    /* A shared cell needs no new edge, even with a full pending edge budget. */
    assert(zmk_kscan_matrix_acquire(&proxy, &reentrant[3], 0, 0) == 0);
    assert(zmk_kscan_matrix_release(&proxy, &reentrant[0]) == 0);
    assert(zmk_kscan_matrix_release(&proxy, &reentrant[3]) == 0);
}

static void ownership_tests(struct device *stranger) {
    struct zmk_kscan_matrix_claim a = {0}, b = {0}, c = {0}, d = {0}, e = {0};
    struct kscan_matrix_data *data = proxy.data;
    assert(zmk_kscan_matrix_acquire(stranger, &a, 0, 0) == -ENODEV);
    assert(zmk_kscan_matrix_release(stranger, &a) == -ENODEV);
    proxy.ready = false;
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == -ENODEV);
    assert(zmk_kscan_matrix_release(&proxy, &a) == -ENODEV);
    proxy.ready = true;
    assert(zmk_kscan_matrix_acquire(&proxy, NULL, 0, 0) == -EINVAL);
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 15, 0) == -EINVAL);
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 3) == -EINVAL);
    assert(zmk_kscan_matrix_release(&proxy, NULL) == -EINVAL);
    assert(zmk_kscan_matrix_release(&proxy, &a) == -EINVAL);
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == -EACCES);
    assert(kscan_matrix_enable_callback(&proxy) == 0);
    data->callback = NULL;
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == -EACCES);
    assert(kscan_matrix_configure(&proxy, callback) == 0);
    calls = 0;
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == 0 && calls == 1);
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == -EALREADY);
    assert(zmk_kscan_matrix_acquire(&proxy, &b, 0, 0) == 0 && calls == 1);
    assert(zmk_kscan_matrix_acquire(&proxy, &c, 0, 1) == 0 && calls == 2);
    assert(zmk_kscan_matrix_acquire(&proxy, &d, 2, 0) == 0 && calls == 3);
    assert(zmk_kscan_matrix_acquire(&proxy, &e, 3, 0) == -ENOSPC);
    assert(zmk_kscan_matrix_release(&proxy, &a) == 0 && calls == 3);
    assert(zmk_kscan_matrix_release(&proxy, &b) == 0 && calls == 4 && !last_pressed);
    assert(zmk_kscan_matrix_release(&proxy, &c) == 0 && calls == 5);
    assert(kscan_matrix_disable_callback(&proxy) == 0);
    assert(zmk_kscan_matrix_release(&proxy, &d) == 0 && calls == 5);
    assert(data->claims == NULL && data->claim_count == 0 && data->reserved_releases == 0);
    assert(kscan_matrix_enable_callback(&proxy) == 0);
    calls = 0;
    check_queue_order = true;
    queue_ordinal = 0;
    during_callback = fill_reentrant_queue;
    assert(zmk_kscan_matrix_acquire(&proxy, &reentrant[0], 0, 0) == 0);
    assert(calls == 2 * ((CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE - 1) / 2) + 2 &&
           queue_ordinal == (unsigned)calls && !last_pressed && last_row == 0);
    check_queue_order = false;
    assert(data->claims == NULL && data->claim_count == 0 && data->reserved_releases == 0);
    assert(data->edge_count == 0 && !data->draining && !data->lock.held);
    /* A callback disappearing does not strand the admitted owner. */
    assert(zmk_kscan_matrix_acquire(&proxy, &a, 0, 0) == 0);
    data->callback = NULL;
    assert(zmk_kscan_matrix_release(&proxy, &a) == 0 && data->reserved_releases == 0);
}

static int conformance(void) {
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
    ownership_tests(&stranger);
    puts("kscan proxy: PASS");
    return 0;
}

#ifdef MATRIX_PERFORMANCE
#include "kscan_bench.h"
#else
int main(void) { return conformance(); }
#endif
