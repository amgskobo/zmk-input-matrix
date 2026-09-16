/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#define ZMK_INPUT_MATRIX_SUBSYSTEM_TOKEN amgskobo__matrix
#define ZMK_INPUT_MATRIX_SUBSYSTEM STRINGIFY(ZMK_INPUT_MATRIX_SUBSYSTEM_TOKEN)

#define ZMK_INPUT_MATRIX_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field
#define ZMK_INPUT_MATRIX_STORAGE_NAME(n, field)                                                \
    "custom_settings/" ZMK_INPUT_MATRIX_SUBSYSTEM "/" ZMK_INPUT_MATRIX_SETTING_KEY(n, field)

#define ZMK_INPUT_MATRIX_ASSERT_NAME_FITS(n, longest_field)                                    \
    BUILD_ASSERT(sizeof(ZMK_INPUT_MATRIX_SETTING_KEY(n, longest_field)) <=                     \
                     CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN,                                   \
                 "matrix node name is too long for its custom-settings key");                 \
    BUILD_ASSERT(sizeof(ZMK_INPUT_MATRIX_STORAGE_NAME(n, longest_field)) <=                    \
                     SETTINGS_MAX_NAME_LEN,                                                    \
                 "matrix node name is too long for its persisted settings name");
