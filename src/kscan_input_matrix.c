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
#include <kscan_input_matrix.h>

LOG_MODULE_REGISTER(kscan_matrix, CONFIG_ZMK_LOG_LEVEL);

struct kscan_matrix_data {
    kscan_callback_t callback;
    bool enabled;
};

/**
 * @brief Public API to report gesture events to the matrix.
 * @param row Matrix row (includes gesture offset).
 * @param column Matrix column.
 * @param pressed Whether the key is pressed or released.
 */
void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row, uint32_t column, bool pressed) {
    struct kscan_matrix_data *data = dev->data;

    if (!data->enabled) {
        return;
    }

    if (data->callback) {
        LOG_DBG("Reporting KSCAN event: Row %u, Column %u, Pressed %d", row, column, pressed);
        data->callback(dev, row, column, pressed);
    }
}

static int kscan_matrix_configure(const struct device *dev, kscan_callback_t callback) {
    struct kscan_matrix_data *data = dev->data;

    if (!callback) {
        LOG_ERR("KSCAN callback cannot be NULL");
        return -EINVAL;
    }

    data->callback = callback;
    LOG_INF("KSCAN callback registered for %s", dev->name);
    return 0;
}

static int kscan_matrix_enable_callback(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    data->enabled = true;
    LOG_DBG("KSCAN matrix %s enabled", dev->name);
    return 0;
}

static int kscan_matrix_disable_callback(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    data->enabled = false;
    LOG_DBG("KSCAN matrix %s disabled", dev->name);
    return 0;
}

static int kscan_matrix_init(const struct device *dev) {
    LOG_INF("KSCAN matrix proxy initialized: %s", dev->name);
    return 0;
}

static const struct kscan_driver_api kscan_matrix_api = {
    .config = kscan_matrix_configure,
    .enable_callback = kscan_matrix_enable_callback,
    .disable_callback = kscan_matrix_disable_callback,
};

#define KSCAN_MATRIX_INIT(n) \
    static struct kscan_matrix_data kscan_matrix_data_##n; \
    DEVICE_DT_INST_DEFINE(n, \
                          kscan_matrix_init, \
                          NULL, \
                          &kscan_matrix_data_##n, \
                          NULL, \
                          POST_KERNEL, \
                          CONFIG_ZMK_KSCAN_INPUT_MATRIX_INIT_PRIORITY, \
                          &kscan_matrix_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_MATRIX_INIT)
