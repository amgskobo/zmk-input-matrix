# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()

zephyr_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_MATRIX src/input_processor_matrix.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_INPUT_MATRIX src/kscan_input_matrix.c)

# Enforce dependency: Input Processor requires KSCAN Proxy
if(CONFIG_ZMK_INPUT_PROCESSOR_MATRIX)
  if(NOT CONFIG_ZMK_KSCAN_INPUT_MATRIX)
    message(FATAL_ERROR "ZMK_INPUT_PROCESSOR_MATRIX requires CONFIG_ZMK_KSCAN_INPUT_MATRIX to be enabled")
  endif()
endif()
