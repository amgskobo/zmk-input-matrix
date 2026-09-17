# Copyright (c) 2025-2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_PROCESSOR_MATRIX OR CONFIG_ZMK_KSCAN_INPUT_MATRIX)
  target_include_directories(app PRIVATE include)
endif()

if(CONFIG_ZMK_INPUT_PROCESSOR_MATRIX)
  target_sources(app PRIVATE src/input_processor_matrix.c)
endif()

if(CONFIG_ZMK_KSCAN_INPUT_MATRIX)
  target_sources(app PRIVATE src/kscan_input_matrix.c)
endif()

if(CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS)
  target_include_directories(app PRIVATE include)
  target_sources(app PRIVATE src/input_processor_matrix_custom_settings.c)
endif()

# Enforce dependency: Input Processor requires KSCAN Proxy
if(CONFIG_ZMK_INPUT_PROCESSOR_MATRIX)
  if(NOT CONFIG_ZMK_KSCAN_INPUT_MATRIX)
    message(FATAL_ERROR "ZMK_INPUT_PROCESSOR_MATRIX requires CONFIG_ZMK_KSCAN_INPUT_MATRIX to be enabled")
  endif()
endif()
