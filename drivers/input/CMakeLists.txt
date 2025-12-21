# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_GRID_MATRIX)
    zephyr_library()
    zephyr_library_sources(input_processor_grid_matrix.c)
endif()

