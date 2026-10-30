/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

static volatile uint64_t perf_callbacks;
static volatile uint64_t perf_checksum;
static void perf_callback(const struct device *dev, uint32_t row, uint32_t column, bool pressed) {
    (void)dev;
    perf_callbacks++;
    perf_checksum += (uint64_t)row + column + pressed;
}

static uint64_t cpu_ns(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec;
}

int main(void) {
    assert(conformance() == 0);
    struct kscan_matrix_data data = {0};
    struct kscan_matrix_config config = {.rows = UINT32_MAX, .columns = UINT32_MAX};
    struct zmk_kscan_matrix_claim claims[4] = {0};
    proxy = (struct device){.data = &data, .config = &config, .ready = true, .name = "measure"};
    assert(kscan_matrix_configure(&proxy, perf_callback) == 0);
    assert(kscan_matrix_enable_callback(&proxy) == 0);
    const unsigned iterations = 500000;
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        unsigned owners = scenario == 0 ? 1 : (scenario == 1 ? 2 : 4);
        bool shared = scenario == 1;
        uint64_t calls_before = perf_callbacks;
        uint64_t locks_before = lock_calls;
        uint64_t start = cpu_ns();
        for (unsigned i = 0; i < iterations; i++) {
            for (unsigned j = 0; j < owners; j++) {
                assert(zmk_kscan_matrix_acquire(&proxy, &claims[j], UINT32_MAX - 1,
                                               shared ? 0 : j) == 0);
            }
            /* Release oldest-first to exercise the full ownership list. */
            for (unsigned j = 0; j < owners; j++) {
                assert(zmk_kscan_matrix_release(&proxy, &claims[j]) == 0);
            }
        }
        uint64_t elapsed = cpu_ns() - start;
        assert(perf_callbacks - calls_before == (uint64_t)iterations * (shared ? 2 : 2 * owners));
        assert(data.claim_count == 0 && data.edge_count == 0 && data.reserved_releases == 0);
        printf("MEASURE scenario=%u iterations=%u cpu_ns=%" PRIu64 " locks=%" PRIu64
               " callbacks=%" PRIu64 " checksum=%" PRIu64 "\n", scenario, iterations,
               elapsed, lock_calls - locks_before, perf_callbacks - calls_before, perf_checksum);
    }
    printf("LAYOUT pointer=%zu edge=%zu output=%zu queue=%u claims=%u\n", sizeof(void *),
           sizeof(struct kscan_matrix_edge), sizeof(struct kscan_matrix_data),
           CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE, CONFIG_ZMK_INPUT_MATRIX_MAX_CLAIMS);
    return 0;
}
