# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
if(CONFIG_MATRIX_KERNEL_TEST)
  target_sources(app PRIVATE main.c)
  target_compile_definitions(app PRIVATE ZIP_MATRIX_TEST_HOLD_HOOK=1)
  if(CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS)
    zephyr_link_libraries(-Wl,--wrap=zmk_custom_setting_read)
  endif()
endif()
