// Run the same independent fixtures through the optimized decoder.
// cc -std=c11 -O1 -fsanitize=address,undefined 
//   runtime/host_verify/lz4_block_fast_test.c -o /tmp/lz4_block_fast_test
// /tmp/lz4_block_fast_test
#include "../lz4_block_fast.h"
#define LZ4_DECODE lz4_block_fast
#include "lz4_block_test.c"
