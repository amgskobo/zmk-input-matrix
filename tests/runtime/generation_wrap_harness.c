/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
int main(void) {
    config = (struct zip_matrix_config){.rows = 1, .columns = 2, .x = 1024, .y = 1024,
                                        .flick_threshold = 50, .long_press_ms = 20,
                                        .kscan_dev = &kscan};
    matrix = (struct device){.config = &config, .data = &data};
    assert(zip_matrix_init(&matrix) == 0);
    data.reset_generation = LONG_MAX;
    for (unsigned i = 0; i < ZIP_MATRIX_STREAM_COUNT; ++i) {
        stream(i)->applied_generation = LONG_MAX;
    }
    press(); frame(100, 100); run_hold(0);
    assert(report_count == 1 && reports[0].pressed);
#if MATRIX_WRAP_LAYER
    zip_matrix_release_hold_on_layer_change(&matrix);
#else
    assert(zip_matrix_set_params(&matrix, &data.params) == 0);
#endif
    assert(data.reset_generation == LONG_MIN);
    assert(report_count == 2 && !reports[1].pressed);
    for (unsigned i = 0; i < ZIP_MATRIX_STREAM_COUNT; ++i) {
        assert(stream(i)->applied_generation == LONG_MIN);
        assert(!stream(i)->contact.is_btn_touch && !stream(i)->hold_work.scheduled);
    }
    /* Replacement after the sign boundary owns its own contact/deadline and
     * emits one complete hold pair. The all-bits-set boundary also reaches0. */
    press(); frame(900, 100); run_hold(0); lift();
    assert(report_count == 4 && reports[2].pressed && !reports[3].pressed);
    data.reset_generation = -1;
    for (unsigned i = 0; i < ZIP_MATRIX_STREAM_COUNT; ++i) {
        stream(i)->applied_generation = -1;
    }
#if MATRIX_WRAP_LAYER
    zip_matrix_release_hold_on_layer_change(&matrix);
#else
    assert(zip_matrix_set_params(&matrix, &data.params) == 0);
#endif
    assert(data.reset_generation == 0);
    for (unsigned i = 0; i < ZIP_MATRIX_STREAM_COUNT; ++i) {
        assert(stream(i)->applied_generation == 0);
    }
    puts("matrix generation sign/full wrap: hold release and replacement PASS");
    return 0;
}
