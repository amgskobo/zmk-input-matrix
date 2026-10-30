/*
 * Copyright (c) 2025 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

/** Zero-initialize once; keep alive until released; do not share between outputs. */
struct zmk_kscan_matrix_claim {
    struct zmk_kscan_matrix_claim *next;
    uint32_t row;
    uint32_t column;
};

/**
 * Acquire one source's ownership of a cell. Only the first owner emits a press.
 * A tap overlapping a hold adds no edge; the last owner alone emits release.
 * Callbacks run outside spinlocks, in admitted edge order, including reentry.
 * Returns -ENODEV/-EINVAL for invalid output/arguments, -EACCES when disabled
 * or unconfigured, -EALREADY for this live claim, and -ENOSPC at the bounded
 * claim/edge limit. Failure changes no ownership; do not release a failed claim.
 * Successful admission reserves room for its eventual final release.
 * Do not mix this ownership API and raw report_event() on the same cells.
 */
int zmk_kscan_matrix_acquire(const struct device *dev, struct zmk_kscan_matrix_claim *claim,
                             uint32_t row, uint32_t column);

/** Remove this claim; -EINVAL if it was not acquired on this output.
 * A successful release cannot fail for capacity. While callbacks are disabled,
 * ownership still clears silently. Disable/reconfigure is not a state replay
 * or old-consumer flush contract, nor a barrier for already admitted callbacks.
 */
int zmk_kscan_matrix_release(const struct device *dev, struct zmk_kscan_matrix_claim *claim);

/**
 * @brief Report a gesture event to the KSCAN matrix proxy.
 *
 * Layout: Each gesture type gets a full (rows x columns) block stacked vertically.
 *
 * @param dev The kscan device instance.
 * @param row KSCAN row index: (gesture * rows) + grid_row.
 * @param column KSCAN column index: grid_column (preserves physical grid layout).
 * @param pressed True if the key is pressed, false if released.
 *
 * Does nothing when dev is not a ready zmk,kscan-input-matrix device, or when
 * the cell is outside its rows and columns.
 */
void zmk_kscan_matrix_report_event(const struct device *dev, uint32_t row, uint32_t column, bool pressed);
