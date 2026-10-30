# SPDX-License-Identifier: MIT
target_sources(app PRIVATE scan.c)
zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_send_mouse_report)

if(CONFIG_MATRIX_IQS_SOURCE_TEST)
    if(NOT EXISTS "${MATRIX_TEST_IQS_ROOT}/src/iqs7211e.c")
        message(FATAL_ERROR "Explicit IQS producer checkout required")
    endif()
    target_sources(app PRIVATE iqs_source.c "${MATRIX_TEST_IQS_ROOT}/src/iqs7211e_filter.c")
    target_compile_definitions(app PRIVATE MATRIX_TEST_IQS_SOURCE="${MATRIX_TEST_IQS_ROOT}/src/iqs7211e.c")
    zephyr_link_libraries(-Wl,--wrap=input_report)
endif()
