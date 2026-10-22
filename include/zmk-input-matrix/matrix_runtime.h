/*
 * Copyright (c) 2026 amgskobo
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
};

int zip_matrix_get_params(const struct device *dev, struct zip_matrix_runtime_params *out);
int zip_matrix_set_params(const struct device *dev,
                          const struct zip_matrix_runtime_params *params);
