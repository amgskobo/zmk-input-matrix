/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#pragma once

#define ZIP_MATRIX_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#ifndef CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES
#define CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES 0
#endif
BUILD_ASSERT(CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES >= 0 &&
                 CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES <= 256,
             "Matrix source allocation bound must be between 0 and 256");
#if CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES > 0
#define ZIP_MATRIX_STREAM_COUNT CONFIG_ZMK_INPUT_MATRIX_MAX_SOURCES
/* Input listener IDs are global DT_INST indices. Match the core's device
 * status gate and every child route, including disabled child nodes. */
#define ZIP_MATRIX_CHECK_PROCESSOR(node, prop, index, source)                                      \
    COND_CODE_1(DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node, prop, index),                          \
                                   zmk_input_processor_matrix),                                \
                (BUILD_ASSERT((source) < ZIP_MATRIX_STREAM_COUNT,                               \
                              "Matrix source limit excludes a referenced listener index");), ())
#define ZIP_MATRIX_CHECK_ROUTE(node, source)                                                       \
    COND_CODE_1(DT_NODE_HAS_PROP(node, input_processors),                                        \
                (DT_FOREACH_PROP_ELEM_VARGS(node, input_processors,                             \
                                             ZIP_MATRIX_CHECK_PROCESSOR, source)), ())
#define ZIP_MATRIX_CHECK_LISTENER(source, compat, unused)                                          \
    COND_CODE_1(DT_NODE_HAS_STATUS(DT_PHANDLE(DT_INST(source, compat), device), okay),             \
                (ZIP_MATRIX_CHECK_ROUTE(DT_INST(source, compat), source)                         \
                 DT_FOREACH_CHILD_VARGS(DT_INST(source, compat), ZIP_MATRIX_CHECK_ROUTE, source)), ())
DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(zmk_input_listener, ZIP_MATRIX_CHECK_LISTENER, unused)
#else
#define ZIP_MATRIX_STREAM_COUNT MAX(ZIP_MATRIX_LISTENER_COUNT, 1)
#endif
