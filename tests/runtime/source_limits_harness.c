/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#include <string.h>

int main(void) {
    assert(ZIP_MATRIX_STREAM_COUNT == EXPECTED_STREAMS &&
           "Matrix must honor the declared source allocation bound");
    config = (struct zip_matrix_config){.rows = 1, .columns = 2, .x = 1024, .y = 1024,
                                        .flick_threshold = 50, .long_press_ms = 20,
                                        .kscan_dev = &kscan};
    matrix = (struct device){.config = &config, .data = &data};
    assert(zip_matrix_init(&matrix) == 0);
    /* NULL still addresses zero; the highest valid source is never compacted
     * into it, even for sparse use or the full uint8 source range. */
    send_to(NULL, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    assert(stream(0)->contact.is_btn_touch);
    struct zmk_input_processor_state highest = {.input_device_index = EXPECTED_STREAMS - 1};
    send_to(&highest, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    assert(stream(EXPECTED_STREAMS - 1)->contact.is_btn_touch);
    if (EXPECTED_STREAMS > 1) {
        send_to(NULL, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
        assert(!stream(0)->contact.is_btn_touch &&
               stream(EXPECTED_STREAMS - 1)->contact.is_btn_touch);
    }
#if EXPECTED_STREAMS < 256
    const struct zip_matrix_data before = data;
    struct zmk_input_processor_state invalid = {.input_device_index = EXPECTED_STREAMS};
    struct input_event event = {.type = INPUT_EV_ABS, .code = INPUT_ABS_X,
                                .value = 123, .sync = true};
    const struct input_event original = event;
    assert(zip_matrix_handle_event(&matrix, &event, 0, 0, &invalid) == ZMK_INPUT_PROC_CONTINUE);
    assert(memcmp(&event, &original, sizeof(event)) == 0);
    assert(memcmp(&data, &before, sizeof(data)) == 0);
#endif
    struct zip_matrix_runtime_params params = data.params;
    assert(zip_matrix_set_params(&matrix, &params) == 0);
    for (unsigned i = 0; i < EXPECTED_STREAMS; ++i) {
        assert(!stream(i)->contact.is_btn_touch && !stream(i)->hold_work.scheduled);
        assert(stream(i)->applied_generation == data.reset_generation);
    }
    puts("matrix source allocation: PASS");
    printf("streams=%d stream-bytes=%zu node-bytes=%zu\n",
           ZIP_MATRIX_STREAM_COUNT, sizeof(struct zip_matrix_stream), sizeof(struct zip_matrix_data));
#if EXPECTED_STREAMS >= 2
    return complete_driver_main();
#else
    return 0;
#endif
}
