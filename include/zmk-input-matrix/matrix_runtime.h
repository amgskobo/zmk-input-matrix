/*
 * Copyright (c) 2026 The ZMK Input Matrix Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct device;

struct zip_matrix_runtime_params {
    bool enabled;
    uint16_t flick_threshold;
    uint16_t long_press_ms;
    bool suppress_abs;
    bool suppress_btn_touch;
    bool suppress_key;
};

int zip_matrix_get_params(const struct device *dev, struct zip_matrix_runtime_params *out);
int zip_matrix_set_params(const struct device *dev,
                          const struct zip_matrix_runtime_params *params);
