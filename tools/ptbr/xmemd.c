/*
 * xmemd: decompresses Xbox 360 XMemCompress "native" LZX buffers (magic 0x0FF512EE), the format of
 * every entry in the Xbox 360 Budokai 3 AFS archives. Host tool for tools/traducao_ptbr.py.
 *
 * Build (Windows, llvm-mingw; libmspack from rexglue's thirdparty tree):
 *   clang -O2 -I<libmspack>/mspack -o xmemd.exe xmemd.c <libmspack>/mspack/lzxd.c
 * traducao_ptbr.py builds it automatically into trabalho/traducao-ptbr/bin/ when missing.
 *
 * Usage:
 *   xmemd <in> <out>   one buffer, file to file
 *   xmemd --pipe       stream: stdin gets [u32 LE length][bytes] records, stdout answers
 *                      [u32 LE length][bytes] (length 0xFFFFFFFF = error). Ends on EOF.
 *
 * Format (big-endian): 0x00 magic, 0x04 version 0x01030000, 0x08 context flags, 0x0C codec flags,
 * 0x10 window size, 0x14 partition size, 0x18 u64 uncompressed size, 0x20 u64 compressed size,
 * 0x28 u32 uncompressed block size, 0x2C u32 max compressed block size. Then blocks of
 * [u32 compressed size][frames], each frame [u16 size] or [0xFF][u16 uncompressed][u16 size],
 * a frame being 32 KB of output. The frames are concatenated into one LZX stream for lzxd.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "mspack.h"
#include "lzx.h"

typedef struct { unsigned char *buf; long len, pos; } memfile;

static struct mspack_file *m_open(struct mspack_system *s, const char *f, int m) { return NULL; }
static void m_close(struct mspack_file *f) {}
static int m_read(struct mspack_file *f, void *b, int n) {
  memfile *m = (memfile *)f; long left = m->len - m->pos; if (left < n) n = (int)left;
  memcpy(b, m->buf + m->pos, n); m->pos += n; return n;
}
static int m_write(struct mspack_file *f, void *b, int n) {
  memfile *m = (memfile *)f; long left = m->len - m->pos; if (left < n) n = (int)left;
  memcpy(m->buf + m->pos, b, n); m->pos += n; return n;
}
static int m_seek(struct mspack_file *f, off_t o, int w) { return -1; }
static off_t m_tell(struct mspack_file *f) { return ((memfile *)f)->pos; }
static void m_msg(struct mspack_file *f, const char *fmt, ...) {}
static void *m_alloc(struct mspack_system *s, size_t n) { return calloc(n, 1); }
static void m_free(void *p) { free(p); }
static void m_copy(void *s, void *d, size_t n) { memcpy(d, s, n); }

static unsigned be32(const unsigned char *p) {
  return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

/* Returns a malloc'd buffer of *out_len bytes, or NULL. */
static unsigned char *decompress(const unsigned char *in, long n, long *out_len) {
  if (n < 0x30 || be32(in) != 0x0FF512EEu) return NULL;
  unsigned window = be32(in + 0x10);
  unsigned long long usize = ((unsigned long long)be32(in + 0x18) << 32) | be32(in + 0x1C);
  unsigned char *stream = malloc(n + 8);
  long slen = 0, p = 0x30;
  while (p + 4 <= n) {
    unsigned bsize = be32(in + p); p += 4;
    if (bsize == 0) break;
    long end = p + bsize; if (end > n) end = n;
    while (p + 2 <= end) {
      unsigned fsize;
      if (in[p] == 0xFF) { if (p + 5 > end) break; fsize = (in[p + 3] << 8) | in[p + 4]; p += 5; }
      else { fsize = (in[p] << 8) | in[p + 1]; p += 2; }
      if (p + fsize > n) { free(stream); return NULL; }
      memcpy(stream + slen, in + p, fsize); slen += fsize; p += fsize;
    }
    p = end;
  }
  memset(stream + slen, 0, 8);
  int bits = 0; while ((1u << bits) < window) bits++;
  struct mspack_system sys = {m_open, m_close, m_read, m_write, m_seek, m_tell, m_msg, m_alloc, m_free, m_copy, NULL};
  unsigned char *out = calloc(usize + 16, 1);
  memfile src = {stream, slen, 0}, dst = {out, (long)usize, 0};
  struct lzxd_stream *z = lzxd_init(&sys, (struct mspack_file *)&src, (struct mspack_file *)&dst, bits, 0, 0x8000, (off_t)usize, 0);
  int rc = z ? lzxd_decompress(z, (off_t)usize) : -1;
  if (z) lzxd_free(z);
  free(stream);
  if (rc != 0) { free(out); return NULL; }
  *out_len = (long)usize;
  return out;
}

static int pipe_mode(void) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  unsigned char hdr[4];
  while (fread(hdr, 1, 4, stdin) == 4) {
    unsigned len = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((unsigned)hdr[3] << 24);
    unsigned char *in = malloc(len + 1);
    if (fread(in, 1, len, stdin) != len) { free(in); return 2; }
    long olen = 0;
    unsigned char *out = decompress(in, len, &olen);
    free(in);
    unsigned ol = out ? (unsigned)olen : 0xFFFFFFFFu;
    unsigned char oh[4] = {ol & 255, (ol >> 8) & 255, (ol >> 16) & 255, ol >> 24};
    fwrite(oh, 1, 4, stdout);
    if (out) { fwrite(out, 1, olen, stdout); free(out); }
    fflush(stdout);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--pipe") == 0) return pipe_mode();
  if (argc != 3) { fprintf(stderr, "usage: xmemd <in> <out> | xmemd --pipe\n"); return 1; }
  FILE *f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *in = malloc(n); if (fread(in, 1, n, f) != (size_t)n) return 2; fclose(f);
  long olen = 0;
  unsigned char *out = decompress(in, n, &olen);
  if (!out) { fprintf(stderr, "not an XMem LZX buffer or corrupt data\n"); return 3; }
  f = fopen(argv[2], "wb"); fwrite(out, 1, olen, f); fclose(f);
  printf("%ld\n", olen);
  return 0;
}
