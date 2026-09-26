/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ARG_UNUSED(x) (void)(x)
#define ZIP_MATRIX_USE_DIAMOND_TAP 1

enum gesture_type {
    GESTURE_TAP, GESTURE_UP, GESTURE_DOWN, GESTURE_LEFT, GESTURE_RIGHT
};

struct zip_matrix_config {
    uint8_t rows;
    uint8_t columns;
    uint16_t x;
    uint16_t y;
    uint16_t flick_threshold;
    bool diamond_tap;
};

/* DRIVER_FUNCTIONS */

int main(void) {
    struct zip_matrix_config grid = {
        .rows = 3U, .columns = 3U, .x = 1024U, .y = 512U,
        .flick_threshold = 50U,
    };
    uint8_t row = 255U, column = 255U;

    assert(clamp_coord_value(-1, 1024U) == 0U);
    assert(clamp_coord_value(1025, 1024U) == 1024U);
    assert(clamp_coord_value(512, 1024U) == 512U);
    calculate_kscan_coordinates(&grid, 0U, 0U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 0U);
    calculate_kscan_coordinates(&grid, 1024U, 512U, GESTURE_RIGHT, &row, &column);
    assert(row == 14U && column == 2U);
    assert(travel_squared(65534, 65534) == 8589410312ULL);
    assert(get_gesture_type(&grid, 30, 40) == GESTURE_DOWN);
    assert(get_gesture_type(&grid, 29, 40) == GESTURE_TAP);
    assert(get_gesture_type(&grid, 50, 0) == GESTURE_RIGHT);
    assert(get_gesture_type(&grid, 49, 0) == GESTURE_TAP);
    /* A normalised tie between the axes falls to the vertical. */
    assert(get_gesture_type(&grid, 60, 30) == GESTURE_DOWN);
    assert(get_gesture_type(&grid, 60, -30) == GESTURE_UP);

    grid.rows = 1U;
    grid.columns = 4U;
    grid.diamond_tap = true;
    calculate_kscan_coordinates(&grid, 512U, 0U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 0U);
    calculate_kscan_coordinates(&grid, 1024U, 256U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 1U);
    /* Mostly right, a little down: each axis normalised by the other's size. */
    calculate_kscan_coordinates(&grid, 1024U, 300U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 1U);
    calculate_kscan_coordinates(&grid, 512U, 512U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 2U);
    calculate_kscan_coordinates(&grid, 0U, 256U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 3U);
    /* The centre and the exact diagonals are ties: vertical, and down
     * unless the finger is above the middle. */
    calculate_kscan_coordinates(&grid, 512U, 256U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 2U);
    calculate_kscan_coordinates(&grid, 1024U, 512U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 2U);
    calculate_kscan_coordinates(&grid, 0U, 0U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 0U);
    assert(get_gesture_type(&grid, 100, 0) == GESTURE_TAP);

    /* Exercise every coordinate of small rectangular and diamond panels. */
    for (uint16_t width = 1U; width <= 12U; width++) {
        for (uint16_t height = 1U; height <= 12U; height++) {
            grid.x = width;
            grid.y = height;
            for (uint16_t y = 0U; y <= height; y++) {
                for (uint16_t x = 0U; x <= width; x++) {
                    grid.diamond_tap = false;
                    grid.rows = 3U;
                    grid.columns = 3U;
                    calculate_kscan_coordinates(&grid, x, y, GESTURE_TAP, &row, &column);
                    assert(row < 3U && column < 3U);
                    grid.diamond_tap = true;
                    grid.rows = 1U;
                    grid.columns = 4U;
                    calculate_kscan_coordinates(&grid, x, y, GESTURE_TAP, &row, &column);
                    assert(row == 0U && column < 4U);
                }
            }
        }
    }

    grid.x = 1024U;
    grid.y = 512U;
    grid.diamond_tap = true;
    grid.rows = 2U;
    grid.columns = 4U;
    calculate_kscan_coordinates(&grid, 100U, 100U, GESTURE_TAP, &row, &column);
    assert(row < 2U && column < 4U);
    grid.rows = 1U;
    grid.columns = 3U;
    calculate_kscan_coordinates(&grid, 100U, 100U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column < 3U);
    grid.columns = 1U;
    calculate_kscan_coordinates(&grid, 100U, 100U, GESTURE_TAP, &row, &column);
    assert(row == 0U && column == 0U);
    grid.diamond_tap = false;
    assert(travel_squared(-30, -40) == 2500ULL);
    assert(get_gesture_type(&grid, -30, -40) == GESTURE_UP);
    assert(get_gesture_type(&grid, -50, 0) == GESTURE_LEFT);
    puts("matrix geometry: PASS");
    return 0;
}
