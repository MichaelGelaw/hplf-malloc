#ifndef HPLF_TEST_PATTERN_H
#define HPLF_TEST_PATTERN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void hplf_test_pattern_fill(void *pointer, size_t size, uint64_t key);
bool hplf_test_pattern_matches(const void *pointer, size_t size, uint64_t key);

#endif
