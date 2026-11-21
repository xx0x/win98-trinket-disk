/* lzxcab: compress a CAB folder's data stream with liblzx (CAB LZX variant).
 * usage: lzxcab <window_bits 15..21> <level> <in.bin> <out.chunks>
 * out.chunks: repeated records { uint32 comp_size; uint32 uncomp_size; bytes[comp_size] } (little endian)
 * One record per 32 KiB input frame = one CFDATA block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "liblzx.h"

static void *a(void *u, size_t n) { (void)u; return malloc(n); }
static void f(void *u, void *p) { (void)u; free(p); }
static void put32(FILE *o, uint32_t v) { unsigned char b[4] = {v, v >> 8, v >> 16, v >> 24}; fwrite(b, 1, 4, o); }

int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: lzxcab bits level in out\n"); return 2; }
    int bits = atoi(argv[1]); int level = atoi(argv[2]);
    FILE *in = fopen(argv[3], "rb"); if (!in) { perror("in"); return 1; }
    fseek(in, 0, SEEK_END); long n = ftell(in); fseek(in, 0, SEEK_SET);
    unsigned char *buf = malloc(n ? n : 1);
    if (fread(buf, 1, n, in) != (size_t)n) { perror("read"); return 1; }
    fclose(in);
    FILE *out = fopen(argv[4], "wb"); if (!out) { perror("out"); return 1; }
    liblzx_compress_properties_t p; memset(&p, 0, sizeof p);
    p.lzx_variant = LIBLZX_VARIANT_CAB_DELTA;
    p.window_size = 1u << bits;
    p.chunk_granularity = 32768;
    p.compression_level = level;
    p.e8_file_size = LIBLZX_CONST_DEFAULT_E8_FILE_SIZE;
    p.alloc_func = a; p.free_func = f;
    liblzx_compressor_t *c = liblzx_compress_create(&p);
    if (!c) { fprintf(stderr, "create failed\n"); return 1; }
    long frames = (n + 32767) / 32768, emitted = 0, pos = 0;
    while (pos < n) {
        size_t d = liblzx_compress_add_input(c, buf + pos, n - pos);
        pos += d;
        const liblzx_output_chunk_t *ch;
        while ((ch = liblzx_compress_get_next_chunk(c)) != NULL) {
            uint32_t un = (emitted == frames - 1) ? (uint32_t)(n - emitted * 32768) : 32768;
            put32(out, (uint32_t)ch->size); put32(out, un); fwrite(ch->data, 1, ch->size, out);
            emitted++; liblzx_compress_release_next_chunk(c);
        }
    }
    liblzx_compress_end_input(c);
    const liblzx_output_chunk_t *ch;
    while ((ch = liblzx_compress_get_next_chunk(c)) != NULL) {
        uint32_t un = (emitted == frames - 1) ? (uint32_t)(n - emitted * 32768) : 32768;
        put32(out, (uint32_t)ch->size); put32(out, un); fwrite(ch->data, 1, ch->size, out);
        emitted++; liblzx_compress_release_next_chunk(c);
    }
    liblzx_compress_destroy(c); fclose(out);
    fprintf(stderr, "input %ld bytes, %ld frames, %ld chunks\n", n, frames, emitted);
    return emitted == frames ? 0 : 3;
}
