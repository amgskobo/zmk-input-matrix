/*
 * Copyright (c) 2025 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_kscan_input_matrix

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <errno.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <kscan_input_matrix.h>

#ifndef CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL
#define CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_matrix_kscan_input_matrix, CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL);

struct kscan_matrix_edge {
    kscan_callback_t callback;
    uint32_t row;
    uint32_t column;
};

struct kscan_matrix_data {
    struct k_spinlock lock;
    kscan_callback_t callback;
    bool enabled;
    struct zmk_kscan_matrix_claim *claims;
    size_t claim_count;
    size_t reserved_releases;
    struct kscan_matrix_edge edges[CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE];
    /* Keep full-width public coordinates without per-edge bool padding. */
    bool pressed[CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE];
    size_t edge_head;
    size_t edge_count;
    bool draining;
};

struct kscan_matrix_config {
    uint32_t rows;
    uint32_t columns;
};

static bool kscan_matrix_device_valid(const struct device *dev);

/* Caller holds the output lock. Each occupied cell reserves one final edge. */
static void kscan_matrix_enqueue(struct kscan_matrix_data *data, uint32_t row,
                                 uint32_t column, bool pressed) {
    size_t tail = (data->edge_head + data->edge_count) % ARRAY_SIZE(data->edges);
    data->edges[tail] = (struct kscan_matrix_edge){
        .callback = data->callback, .row = row, .column = column};
    data->pressed[tail] = pressed;
    data->edge_count++;
}

/* Enter with the caller's output lock; return unlocked. Reuse that
 * critical section to elect the drainer and pop its first edge.
 * Reentrant/concurrent callers append; all callbacks remain outside locks. */
static void kscan_matrix_drain(const struct device *dev, k_spinlock_key_t key) {
    struct kscan_matrix_data *data = dev->data;
    if (data->draining || data->edge_count == 0U) {
        k_spin_unlock(&data->lock, key);
        return;
    }
    data->draining = true;
    while (data->edge_count != 0U) {
        struct kscan_matrix_edge edge = data->edges[data->edge_head];
        bool pressed = data->pressed[data->edge_head];
        data->edge_head = (data->edge_head + 1U) % ARRAY_SIZE(data->edges);
        data->edge_count--;
        k_spin_unlock(&data->lock, key);
        edge.callback(dev, edge.row, edge.column, pressed);
        key = k_spin_lock(&data->lock);
    }
    data->draining = false;
    k_spin_unlock(&data->lock, key);
}

int zmk_kscan_matrix_acquire(const struct device *dev, struct zmk_kscan_matrix_claim *claim,
                             uint32_t row, uint32_t column) {
    if (!kscan_matrix_device_valid(dev) || !device_is_ready(dev)) {
        return -ENODEV;
    }
    const struct kscan_matrix_config *cfg = dev->config;
    if (claim == NULL || row >= cfg->rows || column >= cfg->columns) {
        return -EINVAL;
    }
    struct kscan_matrix_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool occupied = false;
    for (struct zmk_kscan_matrix_claim *p = data->claims; p != NULL; p = p->next) {
        if (p == claim) {
            k_spin_unlock(&data->lock, key);
            return -EALREADY;
        }
        occupied |= p->row == row && p->column == column;
    }
    if (!data->enabled || data->callback == NULL) {
        k_spin_unlock(&data->lock, key);
        return -EACCES;
    }
    if (data->claim_count == CONFIG_ZMK_INPUT_MATRIX_MAX_CLAIMS ||
        (!occupied && data->edge_count + data->reserved_releases + 2U >
                          ARRAY_SIZE(data->edges))) {
        k_spin_unlock(&data->lock, key);
        return -ENOSPC;
    }
    claim->row = row;
    claim->column = column;
    claim->next = data->claims;
    data->claims = claim;
    data->claim_count++;
    if (!occupied) {
        data->reserved_releases++;
        kscan_matrix_enqueue(data, row, column, true);
    }
    kscan_matrix_drain(dev, key);
    return 0;
}

int zmk_kscan_matrix_release(const struct device *dev, struct zmk_kscan_matrix_claim *claim) {
    if (!kscan_matrix_device_valid(dev) || !device_is_ready(dev)) {
        return -ENODEV;
    }
    if (claim == NULL) {
        return -EINVAL;
    }
    struct kscan_matrix_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    struct zmk_kscan_matrix_claim **link = &data->claims;
    while (*link != NULL && *link != claim) {
        link = &(*link)->next;
    }
    if (*link == NULL) {
        k_spin_unlock(&data->lock, key);
        return -EINVAL;
    }
    uint32_t row = claim->row;
    uint32_t column = claim->column;
    *link = claim->next;
    claim->next = NULL;
    data->claim_count--;
    bool occupied = false;
    for (struct zmk_kscan_matrix_claim *p = data->claims; p != NULL; p = p->next) {
        occupied |= p->row == row && p->column == column;
    }
    if (!occupied) {
        data->reserved_releases--;
        if (data->enabled && data->callback != NULL) {
            kscan_matrix_enqueue(data, row, column, false);
        }
    }
    kscan_matrix_drain(dev, key);
    return 0;
}

/**
 * @brief Public API to report gesture events to the matrix.
 * @param row Matrix row (includes gesture offset).
 * @param column Matrix column.
 * @param pressed Whether the key is pressed or released.
 */
void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row, uint32_t column, bool pressed) {
    struct kscan_matrix_data *data;
    const struct kscan_matrix_config *cfg;
    kscan_callback_t callback;

    if (!kscan_matrix_device_valid(dev) || !device_is_ready(dev)) {
        return;
    }

    data = dev->data;
    cfg = dev->config;

    if (row >= cfg->rows || column >= cfg->columns) {
        LOG_WRN("Ignoring out-of-range KSCAN event: row %u/%u, column %u/%u", row, cfg->rows,
                column, cfg->columns);
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    callback = data->enabled ? data->callback : NULL;
    k_spin_unlock(&data->lock, key);

    if (callback) {
        callback(dev, row, column, pressed);
    }
}

static int kscan_matrix_configure(const struct device *dev, kscan_callback_t callback) {
    struct kscan_matrix_data *data = dev->data;

    if (!callback) {
        LOG_ERR("KSCAN callback cannot be NULL");
        return -EINVAL;
    }

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->callback = callback;
    k_spin_unlock(&data->lock, key);
    LOG_INF("KSCAN callback registered for %s", dev->name);
    return 0;
}

static int kscan_matrix_enable_callback(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->enabled = true;
    k_spin_unlock(&data->lock, key);
    LOG_DBG("KSCAN matrix %s enabled", dev->name);
    return 0;
}

static int kscan_matrix_disable_callback(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->enabled = false;
    k_spin_unlock(&data->lock, key);
    LOG_DBG("KSCAN matrix %s disabled", dev->name);
    return 0;
}

static int kscan_matrix_init(const struct device *dev) {
    const struct kscan_matrix_config *cfg = dev->config;

    if (cfg->rows == 0 || cfg->columns == 0) {
        return -EINVAL;
    }

    LOG_INF("KSCAN matrix proxy initialized: %s", dev->name);
    return 0;
}

static const struct kscan_driver_api kscan_matrix_api = {
    .config = kscan_matrix_configure,
    .enable_callback = kscan_matrix_enable_callback,
    .disable_callback = kscan_matrix_disable_callback,
};

#define KSCAN_MATRIX_INIT(n) \
    BUILD_ASSERT(DT_INST_PROP(n, rows) > 0, "zmk,kscan-input-matrix rows must be greater than zero"); \
    BUILD_ASSERT(DT_INST_PROP(n, columns) > 0, \
                 "zmk,kscan-input-matrix columns must be greater than zero"); \
    static struct kscan_matrix_data kscan_matrix_data_##n; \
    static const struct kscan_matrix_config kscan_matrix_config_##n = { \
        .rows = DT_INST_PROP(n, rows), \
        .columns = DT_INST_PROP(n, columns), \
    }; \
    DEVICE_DT_INST_DEFINE(n, \
                          kscan_matrix_init, \
                          NULL, \
                          &kscan_matrix_data_##n, \
                          &kscan_matrix_config_##n, \
                          POST_KERNEL, \
                          CONFIG_KSCAN_INIT_PRIORITY, \
                          &kscan_matrix_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_MATRIX_INIT)

#define KSCAN_MATRIX_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

/* The report path reads dev->data as this driver's data, so it accepts only
 * the devices this driver defined. */
static const struct device *const kscan_matrix_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(KSCAN_MATRIX_DEVICE_REF)};

static bool kscan_matrix_device_valid(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(kscan_matrix_devices); i++) {
        if (kscan_matrix_devices[i] == dev) {
            return true;
        }
    }

    return false;
}
