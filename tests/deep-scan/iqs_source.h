/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>
struct matrix_iqs_edge { int value; int64_t ms; };
void matrix_iqs_init(void);
void matrix_iqs_start(unsigned source, unsigned clicks, bool pause);
void matrix_iqs_resume(unsigned source);
void matrix_iqs_wait(unsigned source, unsigned edges);
unsigned matrix_iqs_count(unsigned source);
struct matrix_iqs_edge matrix_iqs_edge(unsigned source, unsigned index);
