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

LOG_MODULE_REGISTER(kscan_matrix, CONFIG_ZMK_LOG_LEVEL);

struct kscan_matrix_data {
    struct k_spinlock lock;
    kscan_callback_t callback;
    bool enabled;
};

struct kscan_matrix_config {
    uint32_t rows;
    uint32_t columns;
};

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

    if (!device_is_ready(dev)) {
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
        LOG_DBG("Reporting KSCAN event: Row %u, Column %u, Pressed %d", row, column, pressed);
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
