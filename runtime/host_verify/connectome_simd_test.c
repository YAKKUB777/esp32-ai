// Host checks validate the fixtures and rejection paths, not Xtensa instructions.
// cc -std=c11 -O1 -fsanitize=address,undefined 
//   runtime/host_verify/connectome_simd_test.c -o /tmp/connectome_simd_test
// /tmp/connectome_simd_test
#include "../connectome_simd_test.h"
#include <assert.h>
#include <stdio.h>
static unsigned calls;
static int64_t reference(const int16_t *a, const int16_t *b, uint32_t n) {
    assert(!((uintptr_t)a & 15) && !((uintptr_t)b & 15) && n <= 128 && n % 8 == 0);
    calls++;
    int64_t result = 0;
    for (uint32_t i = 0; i < n; i++)
        result += (int32_t)a[i] * b[i];
    return result;
}
static int64_t broken(const int16_t *a, const int16_t *b, uint32_t n) {
    return reference(a, b, n) + (n == 128);
}
int main(void) {
    assert(connectome_dot_selftest(reference));
    assert(calls == 64 * 17);
    assert(!connectome_dot_selftest(broken));
    assert(!connectome_dot_selftest(NULL));
    puts("PASS: 1088 dot fixtures, alignment, signed extremes and negative controls");
}
