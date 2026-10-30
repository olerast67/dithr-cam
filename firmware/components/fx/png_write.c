// Минимальный писатель индексированного PNG. Сжатия нет: данные идут
// блоками deflate типа «stored». Для 1-2 бит на пиксель это всё равно
// маленькие файлы, а время записи почти нулевое.
#include "fx_internal.h"

static uint32_t s_crc_table[256];

static void crc_init(void) {
    if (s_crc_table[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc_table[n] = c;
    }
}

static uint32_t crc_upd(uint32_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) crc = s_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

typedef struct {
    fx_write_fn wr;
    void *user;
    uint32_t crc;
    int err;
} pngw_t;

static void put(pngw_t *w, const void *d, size_t n) {
    if (!w->err && n) {
        w->crc = crc_upd(w->crc, d, n);
        if (w->wr(w->user, d, n) != 0) w->err = 1;
    }
}

static void be32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

static void chunk_begin(pngw_t *w, const char *type, uint32_t len) {
    uint8_t b[4];
    be32(b, len);
    if (!w->err && w->wr(w->user, b, 4) != 0) w->err = 1;  // длина не входит в CRC
    w->crc = 0xFFFFFFFFu;
    put(w, type, 4);
}

static void chunk_end(pngw_t *w) {
    uint8_t b[4];
    be32(b, w->crc ^ 0xFFFFFFFFu);
    if (!w->err && w->wr(w->user, b, 4) != 0) w->err = 1;
}

int fx_png_write_indexed(fx_write_fn wr, void *user, const uint8_t *idx, int w, int h,
                         int bpp, const uint8_t (*pal)[3], int pal_n) {
    crc_init();
    pngw_t pw = {wr, user, 0, 0};
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (wr(user, sig, 8) != 0) return -1;

    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = (uint8_t)bpp;
    ihdr[9] = 3;  // индексированный цвет
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk_begin(&pw, "IHDR", 13);
    put(&pw, ihdr, 13);
    chunk_end(&pw);

    chunk_begin(&pw, "PLTE", (uint32_t)pal_n * 3);
    put(&pw, pal, (size_t)pal_n * 3);
    chunk_end(&pw);

    // Сырые данные: на строку байт фильтра 0 + упакованные пиксели.
    const size_t rowb = ((size_t)w * bpp + 7) / 8;
    const size_t raw = (rowb + 1) * (size_t)h;
    const size_t BLK = 65535;
    const size_t nblk = (raw + BLK - 1) / BLK;
    const uint32_t idat_len = (uint32_t)(2 + nblk * 5 + raw + 4);

    uint8_t *line = malloc(rowb + 1);
    if (!line) return -1;
    chunk_begin(&pw, "IDAT", idat_len);
    static const uint8_t zhdr[2] = {0x78, 0x01};
    put(&pw, zhdr, 2);

    uint32_t a1 = 1, a2 = 0;  // Adler-32
    size_t left_in_blk = 0, emitted = 0;
    const int per = 8 / bpp;
    for (int y = 0; y < h && !pw.err; y++) {
        line[0] = 0;
        memset(line + 1, 0, rowb);
        const uint8_t *s = idx + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const int sh = 8 - bpp * (x % per + 1);
            line[1 + x / per] |= (uint8_t)((s[x] & ((1 << bpp) - 1)) << sh);
        }
        // Пишем строку, разрезая её по границам блоков deflate.
        size_t off = 0;
        while (off < rowb + 1) {
            if (left_in_blk == 0) {
                const size_t blk = FX_MIN(BLK, raw - emitted);
                const uint8_t hdr[5] = {(uint8_t)(emitted + blk == raw ? 1 : 0), (uint8_t)blk, (uint8_t)(blk >> 8),
                                        (uint8_t)~blk, (uint8_t)(~blk >> 8)};
                put(&pw, hdr, 5);
                left_in_blk = blk;
            }
            const size_t n = FX_MIN(left_in_blk, rowb + 1 - off);
            put(&pw, line + off, n);
            for (size_t i = 0; i < n; i++) {
                a1 = (a1 + line[off + i]) % 65521u;
                a2 = (a2 + a1) % 65521u;
            }
            off += n;
            left_in_blk -= n;
            emitted += n;
        }
    }
    free(line);
    uint8_t ad[4];
    be32(ad, (a2 << 16) | a1);
    put(&pw, ad, 4);
    chunk_end(&pw);

    chunk_begin(&pw, "IEND", 0);
    chunk_end(&pw);
    return pw.err ? -1 : 0;
}
