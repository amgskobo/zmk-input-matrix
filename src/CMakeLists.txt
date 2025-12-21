if (CONFIG_ZMK_INPUT_PROCESSOR_MATRIX)
    zephyr_library()
    zephyr_library_sources(zip_matrix.c)
endif()
