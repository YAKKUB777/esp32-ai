// Self-contained regression fixtures. Build from the repository root:
// cc -std=c11 -O1 -ffp-contract=off -fsanitize=address,undefined 
//   runtime/host_verify/connectome_fast_test.c -lm -o /tmp/connectome_fast_test
// /tmp/connectome_fast_test
// Assertions must remain enabled. No model assets or ESP32 required.

#include "../connectome_fast.h"

#include <assert.h>
#include <stdio.h>
#define N 160
static uint8_t image[8192];
static size_t image_size;
static int fail_read;
static unsigned hook_calls, tail_calls;
static int read_image(void *ctx, uint32_t offset, void *dst, size_t count) {
    (void)ctx;
    if (fail_read)
        return 0;
    assert((uint64_t)offset + count <= image_size);
    memcpy(dst, image + offset, count);
    return 1;
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}
static void header(unsigned n, unsigned edges, unsigned block_rows) {
    memcpy(image, "FCL1", 4);
    put32(image + 4, 1);
    put32(image + 8, n);
    put32(image + 12, edges);
    put32(image + 16, block_rows);
    for (unsigned i = 0; i < n; i++)
        put32(image + 20 + i * 4, i);
    image_size = 20 + n * 4;
}
static void block(const uint8_t *raw, size_t size, unsigned rows, unsigned edges) {
    uint8_t *meta = image + image_size;
    image_size += 16;
    uint8_t *dst = image + image_size, *begin = dst;
    *dst++ = (uint8_t)((size < 15 ? size : 15) << 4);
    if (size >= 15) {
        size_t extra = size - 15;
        while (extra >= 255) {
            *dst++ = 255;
            extra -= 255;
        }
        *dst++ = (uint8_t)extra;
    }
    memcpy(dst, raw, size);
    dst += size;
    size_t bytes = (size_t)(dst - begin);
    put32(meta, rows);
    put32(meta + 4, edges);
    put32(meta + 8, (uint32_t)size);
    put32(meta + 12, (uint32_t)bytes);
    image_size += bytes;
}
static void make(unsigned edges) {
    // Fixed fixture: one row with 129 incoming edges, then empty rows.
    // Emit degree, index deltas and 32767 contact counts directly; this test
    // does not depend on the research exporter or any downloaded weights.
    header(N, edges, 32);
    for (unsigned row = 0; row < N; row += 32) {
        uint8_t raw[2048];
        size_t bytes = 0;
        if (row == 0 && edges) {
            raw[bytes++] = 0x81;
            raw[bytes++] = 0x01;
        } else
            raw[bytes++] = 0;
        for (unsigned i = 1; i < 32; i++)
            raw[bytes++] = 0;
        if (row == 0) {
            for (unsigned i = 0; i < edges; i++)
                raw[bytes++] = i ? 1 : 0;
            for (unsigned i = 0; i < edges; i++) {
                raw[bytes++] = 0xff;
                raw[bytes++] = 0xff;
                raw[bytes++] = 0x01;
            }
        }
        block(raw, bytes, 32, row ? 0 : edges);
    }
}
static int64_t spy(const int16_t *a, const int16_t *b, uint32_t n) {
    assert(!((uintptr_t)a & 15) && !((uintptr_t)b & 15) && n && n <= 128 && n % 8 == 0);
    hook_calls++;
    if (n == 8) {
        for (unsigned i = 1; i < 8; i++)
            assert(a[i] == 0 && b[i] == 0);
        tail_calls++;
    }
    return fast_dot_scalar(a, b, n);
}
static FastRow rows[N];
static FastBlock blocks[5];
static uint8_t compressed[2048], raw[2048], arena[8192] __attribute__((aligned(16)));
static uint32_t degrees[32];
static FastWorkspace ws;
static float state[N], input[N], expected[N], actual[N];
static int32_t quant[N];
static FastGraph graph_for(unsigned n, unsigned edges, unsigned block_rows) {
    ConnectomeModel m = {
        n, edges, (uint32_t)image_size, block_rows, 2048, 2048, read_image, NULL, NULL, NULL, 1};
    FastGraph g = {m, rows, blocks, 0, 0, image, 0};
    return g;
}
int main(void) {
    ws.stream.compressed = compressed;
    ws.stream.raw = raw;
    ws.stream.degrees = degrees;
    for (unsigned i = 0; i < N; i++) {
        state[i] = (i % 3 == 0) ? -1.f : ((i % 3 == 1) ? 1.f : .1234567f);
        input[i] = .05f;
    }
    unsigned checks = 0;
    for (unsigned empty = 0; empty < 2; empty++)
        for (unsigned staged = 0; staged < 2; staged++)
            for (unsigned budget = 0; budget < 2; budget++) {
                unsigned edges = empty ? 0 : 129;
                make(edges);
                FastGraph g = graph_for(N, edges, 32);
                size_t capacity = budget ? sizeof(arena) : 0;
                assert(fast_prepare(&g, &ws, budget ? arena : NULL, capacity, staged) == 0);
                assert(g.count == 5);
                if (empty && staged)
                    for (unsigned i = 0; i < g.count; i++)
                        assert(!g.blocks[i].cache);
                assert(connectome_step(&g.model, &ws.stream, state, input, expected) == 0);
                assert(fast_quantize(state, quant, N) == 0);
                for (unsigned q = 0; q < 2; q++)
                    for (unsigned cached = 0; cached < 2; cached++)
                        for (unsigned mapped = 0; mapped < 2; mapped++) {
                            g.use_mapping = mapped;
                            FastProfile a, b;
                            assert(fast_range(&g, &ws, 0, g.split, state, input, actual, state,
                                              q ? quant : NULL, spy, cached, NULL, &a) == 0);
                            assert(fast_range(&g, &ws, g.split, g.count, state, input, actual,
                                              state, q ? quant : NULL, spy, cached, NULL, &b) == 0);
                            assert(a.blocks + b.blocks == 5);
                            for (unsigned i = 0; i < N; i++)
                                assert(fabsf(expected[i] - actual[i]) < 3e-5f);
                            checks++;
                        }
                FastProfile p;
                assert(fast_range(&g, &ws, 1, 0, state, input, actual, state, NULL, spy, 0, NULL,
                                  &p) == 3);
                assert(fast_range(&g, &ws, 0, g.count + 1, state, input, actual, state, NULL, spy,
                                  0, NULL, &p) == 3);
                assert(fast_range(&g, &ws, 0, g.count, state, input, actual, NULL, quant, NULL, 0,
                                  NULL, &p) == 3);
                assert(fast_range(&g, &ws, 0, g.count, state, input, actual, state, NULL, spy, 0,
                                  NULL, NULL) == 3);
                fail_read = 1;
                g.use_mapping = 0;
                assert(fast_range(&g, &ws, 0, g.count, state, input, actual, state, NULL, spy, 0,
                                  NULL, &p) == 1);
                fail_read = 0;
            }
    assert(hook_calls && tail_calls);
    float invalids[] = {NAN, INFINITY, 1.01f, -1.01f};
    for (unsigned i = 0; i < 4; i++)
        assert(fast_quantize(invalids + i, quant, 1) == 1);
    assert(fast_quantize(NULL, quant, N) == 1);
    assert(fast_quantize(state, NULL, N) == 1);
    const uint8_t bad_index[] = {1, 128, 128, 128, 0, 1}, bad_weight[] = {1, 0, 129, 128, 128, 0},
                  large_weight[] = {1, 0, 128, 128, 2};
    const uint8_t *bad[] = {bad_index, bad_weight, large_weight};
    const size_t sizes[] = {sizeof(bad_index), sizeof(bad_weight), sizeof(large_weight)};
    int codes[] = {6, 7, 7};
    for (unsigned i = 0; i < 3; i++) {
        header(1, 1, 1);
        block(bad[i], sizes[i], 1, 1);
        FastGraph g = graph_for(1, 1, 1);
        assert(fast_prepare(&g, &ws, arena, sizeof(arena), 0) == codes[i]);
    }
    make(129);
    FastGraph g = graph_for(N, 129, 32);
    assert(fast_prepare(NULL, &ws, arena, sizeof(arena), 0) == 11);
    assert(fast_prepare(&g, NULL, arena, sizeof(arena), 0) == 11);
    assert(fast_prepare(&g, &ws, NULL, 16, 0) == 11);
    assert(fast_prepare(&g, &ws, arena + 1, 16, 0) == 11);
    printf("PASS: %u execution combinations, padded dot hooks, empty graphs/cache, argument and "
           "varint checks\n",
           checks);
    return 0;
}
