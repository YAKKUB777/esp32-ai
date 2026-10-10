// Decoder bounds and overlapping-match fixtures. Assertions must be enabled.
// cc -std=c11 -O1 -fsanitize=address,undefined 
//   runtime/host_verify/lz4_block_test.c -o /tmp/lz4_block_test
// /tmp/lz4_block_test
#ifndef LZ4_DECODE
#include "../lz4_block.h"
#define LZ4_DECODE lz4_block
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t input[128], output[256];
    // One literal followed by a long offset-1 match, then five literals.
    // Expected text is computed independently of the decoder's copy loop.
    for (unsigned count = 4; count < 100; count++) {
        size_t length = 0;
        input[length++] = 0x10 | (count - 4 < 15 ? count - 4 : 15);
        input[length++] = 'A';
        input[length++] = 1;
        input[length++] = 0;
        if (count >= 19)
            input[length++] = count - 19;
        input[length++] = 0x50;
        memcpy(input + length, "BCDEF", 5);
        length += 5;
        memset(output, 0xa5, sizeof(output));
        assert(LZ4_DECODE(input, length, output, count + 6));
        for (unsigned i = 0; i <= count; i++)
            assert(output[i] == 'A');
        assert(!memcmp(output + count + 1, "BCDEF", 5));
        assert(output[count + 6] == 0xa5);
        assert(!LZ4_DECODE(input, length, output, count + 5));
    }
    uint32_t rng = 12094;
    for (unsigned test = 0; test < 50000; test++) {
        for (unsigned i = 0; i < sizeof(input); i++) {
            rng = rng * 1664525u + 1013904223u;
            input[i] = rng >> 24;
        }
        size_t length = test % 129, capacity = (test / 129) % 129;
        memset(output, 0xa5, sizeof(output));
        // ASan checks reads; the sentinel also checks writes within the array
        // but outside the decoder's advertised destination size.
        (void)LZ4_DECODE(input, length, output, capacity);
        for (size_t i = capacity; i < sizeof(output); i++)
            assert(output[i] == 0xa5);
    }
    assert(!LZ4_DECODE(NULL, 1, output, sizeof(output)));
    assert(!LZ4_DECODE(input, 1, NULL, sizeof(output)));
    puts("PASS: 96 overlapping matches and 50000 malformed-block bounds checks");
}
