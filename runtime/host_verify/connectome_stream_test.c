// Self-contained regression fixtures. Build from the repository root:
// cc -std=c11 -O1 -ffp-contract=off -fsanitize=address,undefined 
//   runtime/host_verify/connectome_stream_test.c -lz -lm -o /tmp/connectome_stream_test
// /tmp/connectome_stream_test
// Assertions must remain enabled. No model assets or ESP32 required.

#include "../connectome_stream.h"

#include <assert.h>
#include <stdio.h>
#include <zlib.h>
typedef struct {
    uint8_t data[256];
    uint32_t size;
    int fail_offset, inflate_fail;
    unsigned yields, reads;
} Context;
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static int read_image(void *p, uint32_t offset, void *dst, size_t n) {
    Context *c = (Context *)p;
    c->reads++;
    if (c->fail_offset >= 0 && offset == (uint32_t)c->fail_offset)
        return 0;
    assert((uint64_t)offset + n <= c->size);
    memcpy(dst, c->data + offset, n);
    return 1;
}
static int inflate_image(void *p, const uint8_t *s, size_t n, uint8_t *d, size_t cap) {
    Context *c = (Context *)p;
    if (c->inflate_fail)
        return 0;
    uLongf output = cap;
    return uncompress(d, &output, s, n) == Z_OK && output == cap;
}
static void cooperate(void *p) { ((Context *)p)->yields++; }
static ConnectomeModel make(Context *c, int lz4) {
    memset(c, 0, sizeof(*c));
    c->fail_offset = -1;
    memcpy(c->data, lz4 ? "FCL1" : "FCZ1", 4);
    put32(c->data + 4, 1);
    put32(c->data + 8, 2);
    put32(c->data + 12, 2);
    put32(c->data + 16, 1);
    put32(c->data + 20, 0);
    put32(c->data + 24, 1);
    size_t pos = 28;
    for (unsigned row = 0; row < 2; row++) {
        uint8_t raw[] = {1, (uint8_t)(1 - row), (uint8_t)(row ? 7 : 3)}, packed[64];
        uLongf size = sizeof(packed);
        if (lz4) {
            packed[0] = 0x30;
            memcpy(packed + 1, raw, 3);
            size = 4;
        } else
            assert(compress(packed, &size, raw, 3) == Z_OK);
        put32(c->data + pos, 1);
        put32(c->data + pos + 4, 1);
        put32(c->data + pos + 8, 3);
        put32(c->data + pos + 12, (uint32_t)size);
        pos += 16;
        memcpy(c->data + pos, packed, size);
        pos += size;
    }
    c->size = (uint32_t)pos;
    ConnectomeModel m = {2, 2, c->size, 1, 64, 64, read_image, inflate_image, cooperate, c, lz4};
    return m;
}
static unsigned checks;
static void check(int actual, int expected) {
    assert(actual == expected);
    checks++;
}
int main(void) {
    Context c;
    uint8_t compressed[64], raw[64];
    uint32_t degrees[1];
    ConnectomeWorkspace w = {compressed, raw, degrees};
    float state[2] = {.3f, -.5f}, input[2] = {.2f, -.1f}, next[2];
    for (int lz4 = 0; lz4 <= 1; lz4++) {
        ConnectomeModel m = make(&c, lz4);
        next[0] = next[1] = 123;
        check(connectome_step(&m, &w, NULL, input, next), 0);
        assert(next[0] == 123 && next[1] == 123);
        assert(c.yields == 2 && c.reads == 5);
        check(connectome_step(&m, &w, state, input, next), 0);
        assert(fabsf(next[0] - (.5f * state[0] + .5f * tanhf(.7f * state[1] + input[0]))) < 1e-7f);
        assert(fabsf(next[1] - (.5f * state[1] + .5f * tanhf(.7f * state[0] + input[1]))) < 1e-7f);
        if (!lz4) {
            c.inflate_fail = 1;
            check(connectome_step(&m, &w, NULL, NULL, NULL), 15);
            c.inflate_fail = 0;
            m.inflate = NULL;
            check(connectome_step(&m, &w, NULL, NULL, NULL), 18);
        }
    }
    ConnectomeModel m = make(&c, 1);
    check(connectome_step(NULL, &w, NULL, NULL, NULL), 18);
    check(connectome_step(&m, NULL, NULL, NULL, NULL), 18);
    check(connectome_step(&m, &w, state, NULL, next), 18);
    check(connectome_step(&m, &w, state, input, NULL), 18);
    w.raw = NULL;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 18);
    w.raw = raw;
    m.read = NULL;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 18);
    m = make(&c, 1);
    m.max_raw = 0;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 18);
    m = make(&c, 1);
    for (unsigned pos = 0; pos < 20; pos += 4) {
        uint32_t saved = connectome_u32(c.data + pos);
        put32(c.data + pos, UINT32_MAX);
        check(connectome_step(&m, &w, NULL, NULL, NULL), 11);
        put32(c.data + pos, saved);
    }
    m.bytes = 19;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 10);
    m = make(&c, 1);
    c.fail_offset = 0;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 10);
    c.fail_offset = 28;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 12);
    c.fail_offset = 44;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 14);
    c.fail_offset = -1;
    m.bytes = 28;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 12);
    m = make(&c, 1);
    m.bytes = 47;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 13);
    m = make(&c, 1);
    for (unsigned pos = 28; pos <= 40; pos += 4) {
        if (pos == 32)
            continue;
        uint32_t saved = connectome_u32(c.data + pos);
        put32(c.data + pos, 0);
        check(connectome_step(&m, &w, NULL, NULL, NULL), 13);
        put32(c.data + pos, UINT32_MAX);
        check(connectome_step(&m, &w, NULL, NULL, NULL), 13);
        put32(c.data + pos, saved);
    }
    c.data[44] = 0;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 15);
    m = make(&c, 1);
    c.data[47] = 0;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 104);
    m = make(&c, 1);
    put32(c.data + 32, 2);
    check(connectome_step(&m, &w, NULL, NULL, NULL), 102);
    m = make(&c, 1);
    m.edges = 1;
    put32(c.data + 12, 1);
    check(connectome_step(&m, &w, NULL, NULL, NULL), 16);
    m = make(&c, 1);
    m.edges = 3;
    put32(c.data + 12, 3);
    check(connectome_step(&m, &w, NULL, NULL, NULL), 17);
    m = make(&c, 1);
    c.data[c.size++] = 0;
    m.bytes = c.size;
    check(connectome_step(&m, &w, NULL, NULL, NULL), 17);
    assert(connectome_hash("hello", 5) == 0x4f9f2cabu);
    assert(connectome_hash_more(connectome_hash("he", 2), "llo", 3) == connectome_hash("hello", 5));
    printf("PASS: %u stream checks, FCL1/FCZ1, callback failures, fingerprints\n", checks);
    return 0;
}
