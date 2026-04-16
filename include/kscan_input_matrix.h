/*
 * Copyright (c) 2025 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Report a gesture event to the KSCAN matrix proxy.
 *
 * Layout: Each gesture type gets a full (rows x columns) block stacked vertically.
 *
 * @param dev The kscan device instance.
 * @param row KSCAN row index: (gesture * rows) + grid_row.
 * @param column KSCAN column index: grid_column (preserves physical grid layout).
 * @param pressed True if the key is pressed, false if released.
 */
void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row, uint32_t column, bool pressed);
