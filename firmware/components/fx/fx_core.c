// Общая часть: таблица стилей, размытие, Собель, уровни, рисование.
#include <math.h>

#include "fx_internal.h"

const uint8_t fx_bayer4[4][4] = {
    {8, 136, 40, 168},
    {200, 72, 232, 104},
    {56, 184, 24, 152},
    {248, 120, 216, 88},
};

const uint8_t fx_bayer8[8][8] = {
    {2, 130, 34, 162, 10, 138, 42, 170},     {194, 66, 226, 98, 202, 74, 234, 106},
    {50, 178, 18, 146, 58, 186, 26, 154},    {242, 114, 210, 82, 250, 122, 218, 90},
    {14, 142, 46, 174, 6, 134, 38, 166},     {206, 78, 238, 110, 198, 70, 230, 102},
    {62, 190, 30, 158, 54, 182, 22, 150},    {254, 126, 222, 94, 246, 118, 214, 86},
};

static const char *const V_NEON[] = {"VAPOR", "MANGA"};
static const char *const V_Y2K[] = {"ICE", "CHROME", "SUNSET", "TOXIC"};
static const char *const V_MELT[] = {"DRIP", "RISE", "WIND", "MOLTEN"};
static const char *const V_GHOST[] = {"RGB", "ECHO", "XRAY"};
static const char *const V_MOSH[] = {"CRPT", "BLOCK"};
static const char *const V_DMG[] = {"DMG", "POCKET", "SGB", "PRINT"};
static const char *const V_INK[] = {"ENGRV", "RISO"};
static const char *const V_TIDE[] = {"SLIT", "RADIAL", "LIQUID"};
static const char *const V_PULSE[] = {"SCOPE", "CHROMA"};
static const char *const V_SHARD[] = {"GLASS", "MOSAIC", "PRISM"};
static const char *const V_HOLO[] = {"CLOUD", "WIRE", "SCAN"};
static const char *const V_LOOP[] = {"SPIRAL", "VORTEX"};
static const char *const V_TOPO[] = {"MAP"};
static const char *const V_NONE[] = {"-"};

#define VARS(v) (uint8_t)(sizeof(v) / sizeof((v)[0])), v

static const fx_info_t s_info[FX_COUNT] = {
    [FX_NEON] = {"NEON", "ANIME CEL", 1, FX_OUT_RGB, VARS(V_NEON)},
    [FX_Y2K] = {"Y2K", "BURN + HUD", 1, FX_OUT_RGB, VARS(V_Y2K)},
    [FX_MELT] = {"MELT", "PIXEL SORT", 1, FX_OUT_RGB, VARS(V_MELT)},
    [FX_GHOST] = {"GHOST", "TIME SPLIT", 3, FX_OUT_RGB, VARS(V_GHOST)},
    [FX_MOSH] = {"MOSH", "DATA GLITCH", 1, FX_OUT_RGB, VARS(V_MOSH)},
    [FX_DMG] = {"DMG", "GAMEBOY CAM", 1, FX_OUT_PAL, VARS(V_DMG)},
    [FX_INK] = {"INK", "PRINT SHOP", 1, FX_OUT_PAL, VARS(V_INK)},
    [FX_TIDE] = {"TIDE", "TIME SCAN", FX_TIDE_FRAMES, FX_OUT_RGB, VARS(V_TIDE)},
    [FX_PULSE] = {"PULSE", "SIGNAL LINES", 1, FX_OUT_RGB, VARS(V_PULSE)},
    [FX_SHARD] = {"SHARD", "BROKEN GLASS", 1, FX_OUT_RGB, VARS(V_SHARD)},
    [FX_HOLO] = {"HOLO", "VOLUME SCAN", 1, FX_OUT_RGB, VARS(V_HOLO)},
    [FX_LOOP] = {"LOOP", "INFINITE ZOOM", 1, FX_OUT_RGB, VARS(V_LOOP)},
    [FX_TOPO] = {"TOPO", "HEIGHT MAP", 1, FX_OUT_RGB, VARS(V_TOPO)},
    [FX_MIX] = {"MIX", "RANDOM ALL", 1, FX_OUT_RGB, VARS(V_NONE)},
    [FX_RAW] = {"RAW", "NO FX", 1, FX_OUT_NONE, VARS(V_NONE)},
};

const fx_info_t *fx_info(fx_style_t s) { return &s_info[(unsigned)s < FX_COUNT ? s : FX_RAW]; }

int fx_pick_variant(fx_style_t s, int want, uint32_t seed) {
    const int n = fx_info(s)->nvar;
    if (want >= 0 && want < n) return want;
    fx_rand(&seed);
    fx_rand(&seed);
    return (int)(seed % (uint32_t)n);
}

size_t fx_scratch_size(int w, int h) { return (size_t)w * h * 2 + 65536; }

// ---------------------------------------------------------------- потоки
static fx_par_fn s_par;

void fx_set_parallel(fx_par_fn par) { s_par = par; }

void fx_par_rows(fx_rows_fn fn, void *ctx, int h) {
    if (s_par && h >= 32)
        s_par(fn, ctx, h);
    else
        fn(ctx, 0, h);
}

void fx_progress(fx_job_t *j, int pct) {
    if (j->progress) j->progress(pct, j->user);
}

void fx_render(fx_style_t s, fx_job_t *j) {
    j->idx = NULL;
    j->pal_n = 0;
    j->bpp = 0;
    j->out_w = j->img.w;
    j->out_h = j->img.h;
    if (j->seed == 0) j->seed = 0x9E3779B9u;
    if (j->variant < 0 || j->variant >= fx_info(s)->nvar) j->variant = fx_pick_variant(s, -1, j->seed);
    switch (s) {
        case FX_NEON: fx_neon_render(j); break;
        case FX_Y2K: fx_y2k_render(j); break;
        case FX_MELT: fx_melt_render(j); break;
        case FX_GHOST: fx_ghost_render(j); break;
        case FX_MOSH: fx_mosh_render(j); break;
        case FX_DMG: fx_dmg_render(j); break;
        case FX_INK: fx_ink_render(j); break;
        case FX_TIDE: fx_tide_render(j); break;
        case FX_PULSE: fx_pulse_render(j); break;
        case FX_SHARD: fx_shard_render(j); break;
        case FX_HOLO: fx_holo_render(j); break;
        case FX_LOOP: fx_loop_render(j); break;
        case FX_TOPO: fx_topo_render(j); break;
        default: break;
    }
    fx_progress(j, 100);
}

// ---------------------------------------------------------------- плоскости
typedef struct {
    const fx_rgb_t *im;
    uint8_t *L;
} luma_ctx_t;

static void luma_rows(void *c, int y0, int y1) {
    luma_ctx_t *k = c;
    const int w = k->im->w;
    for (int y = y0; y < y1; y++) {
        const uint8_t *s = k->im->p + (size_t)y * w * 3;
        uint8_t *d = k->L + (size_t)y * w;
        for (int x = 0; x < w; x++, s += 3) d[x] = fx_luma(s);
    }
}

void fx_luma_plane(const fx_rgb_t *im, uint8_t *L) {
    luma_ctx_t k = {im, L};
    fx_par_rows(luma_rows, &k, im->h);
}

// Размытие окном 2r+1 по строкам и по столбцам. Вертикальный проход тоже
// идёт по строкам: суммы столбцов живут в массиве, а исходные строки, которые
// уже перезаписаны, хранятся в кольце из r+2 строк. Так PSRAM читается
// подряд, а не прыжками по 2400 байт, и кеш не промахивается на каждом пикселе.
void fx_blur_plane(uint8_t *p, int w, int h, int ch, int r) {
    if (r <= 0 || w <= 0 || h <= 0) return;
    const int n = 2 * r + 1;
    const uint32_t inv = (65536u + (uint32_t)n / 2) / (uint32_t)n;
    const int rowb = w * ch;
    const int R = r + 2;
    uint8_t *line = malloc((size_t)rowb);
    uint8_t *ring = malloc((size_t)R * rowb);
    uint32_t *sum = malloc(sizeof(uint32_t) * (size_t)rowb);
    if (!line || !ring || !sum) goto out;

    for (int y = 0; y < h; y++) {
        uint8_t *row = p + (size_t)y * rowb;
        memcpy(line, row, (size_t)rowb);
        for (int c = 0; c < ch; c++) {
            uint32_t s = 0;
            for (int k = -r; k <= r; k++) s += line[fx_clampi(k, 0, w - 1) * ch + c];
            for (int x = 0; x < w; x++) {
                row[x * ch + c] = (uint8_t)((s * inv + 32768u) >> 16);
                const int xa = x + r + 1 < w ? x + r + 1 : w - 1;
                const int xs = x - r > 0 ? x - r : 0;
                s += (uint32_t)line[xa * ch + c] - line[xs * ch + c];
            }
        }
    }

    memset(sum, 0, sizeof(uint32_t) * (size_t)rowb);
    for (int k = -r; k <= r; k++) {
        const uint8_t *src = p + (size_t)fx_clampi(k, 0, h - 1) * rowb;
        for (int i = 0; i < rowb; i++) sum[i] += src[i];
    }
    for (int y = 0; y < h; y++) {
        uint8_t *row = p + (size_t)y * rowb;
        memcpy(ring + (size_t)(y % R) * rowb, row, (size_t)rowb);
        for (int i = 0; i < rowb; i++) row[i] = (uint8_t)((sum[i] * inv + 32768u) >> 16);
        if (y == h - 1) break;
        const int ya = y + r + 1 < h ? y + r + 1 : h - 1;
        const int ys = y - r;
        const uint8_t *add = p + (size_t)ya * rowb;
        const uint8_t *sub = ring + (size_t)(ys > 0 ? ys % R : 0) * rowb;
        for (int i = 0; i < rowb; i++) sum[i] += (uint32_t)add[i] - sub[i];
    }
out:
    free(line);
    free(ring);
    free(sum);
}

typedef struct {
    const uint8_t *L;
    uint8_t *M;
    int w, h;
} sobel_ctx_t;

static void sobel_rows(void *c, int y0, int y1) {
    sobel_ctx_t *k = c;
    const int w = k->w, h = k->h;
    for (int y = y0; y < y1; y++) {
        const uint8_t *a = k->L + (size_t)(y > 0 ? y - 1 : 0) * w;
        const uint8_t *b = k->L + (size_t)y * w;
        const uint8_t *c2 = k->L + (size_t)(y < h - 1 ? y + 1 : h - 1) * w;
        uint8_t *o = k->M + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const int xl = x > 0 ? x - 1 : 0, xr = x < w - 1 ? x + 1 : w - 1;
            const int gx = (a[xr] + 2 * b[xr] + c2[xr]) - (a[xl] + 2 * b[xl] + c2[xl]);
            const int gy = (c2[xl] + 2 * c2[x] + c2[xr]) - (a[xl] + 2 * a[x] + a[xr]);
            const int m = (FX_ABS(gx) + FX_ABS(gy)) >> 2;
            o[x] = (uint8_t)(m > 255 ? 255 : m);
        }
    }
}

void fx_sobel(const uint8_t *L, uint8_t *M, int w, int h) {
    sobel_ctx_t k = {L, M, w, h};
    fx_par_rows(sobel_rows, &k, h);
}

void fx_percentiles(const uint8_t *L, int n, int lo_x10, int hi_x10, int *lo, int *hi) {
    uint32_t hist[256] = {0};
    for (int i = 0; i < n; i++) hist[L[i]]++;
    const uint32_t tlo = (uint32_t)((int64_t)n * lo_x10 / 1000);
    const uint32_t thi = (uint32_t)((int64_t)n * hi_x10 / 1000);
    uint32_t acc = 0;
    *lo = 0;
    *hi = 255;
    int got_lo = 0;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (!got_lo && acc > tlo) {
            *lo = v;
            got_lo = 1;
        }
        if (acc >= thi) {
            *hi = v;
            break;
        }
    }
    if (*hi <= *lo) *hi = *lo + 1;
}

void fx_levels_lut(uint8_t lut[256], int lo, int hi) {
    const int span = FX_MAX(hi - lo, 1);
    for (int v = 0; v < 256; v++) lut[v] = fx_u8((v - lo) * 255 / span);
}

// ---------------------------------------------------------------- масштаб
void fx_resize_gray(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    // Координаты в формате 16.16, центры пикселей совмещены.
    const int32_t fx = (int32_t)(((int64_t)sw << 16) / dw);
    const int32_t fy = (int32_t)(((int64_t)sh << 16) / dh);
    for (int y = 0; y < dh; y++) {
        int32_t syf = (int32_t)((y * (int64_t)fy) + fy / 2 - 32768);
        if (syf < 0) syf = 0;
        int sy = syf >> 16;
        if (sy >= sh - 1) {
            sy = sh - 2;
            syf = (sy << 16) | 0xFFFF;
        }
        const int wy = (syf >> 8) & 0xFF;
        const uint8_t *r0 = src + (size_t)sy * sw, *r1 = r0 + sw;
        for (int x = 0; x < dw; x++) {
            int32_t sxf = (int32_t)((x * (int64_t)fx) + fx / 2 - 32768);
            if (sxf < 0) sxf = 0;
            int sx = sxf >> 16;
            if (sx >= sw - 1) {
                sx = sw - 2;
                sxf = (sx << 16) | 0xFFFF;
            }
            const int wx = (sxf >> 8) & 0xFF;
            const int top = r0[sx] * (256 - wx) + r0[sx + 1] * wx;
            const int bot = r1[sx] * (256 - wx) + r1[sx + 1] * wx;
            dst[y * dw + x] = (uint8_t)((top * (256 - wy) + bot * wy + 32768) >> 16);
        }
    }
}

// ---------------------------------------------------------------- рисование
void fx_fill_rect(fx_rgb_t *im, int x, int y, int w, int h, const uint8_t c[3]) {
    int x0 = FX_MAX(x, 0), y0 = FX_MAX(y, 0);
    int x1 = FX_MIN(x + w, im->w), y1 = FX_MIN(y + h, im->h);
    for (int yy = y0; yy < y1; yy++) {
        uint8_t *d = im->p + ((size_t)yy * im->w + x0) * 3;
        for (int xx = x0; xx < x1; xx++, d += 3) {
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
        }
    }
}

void fx_line(fx_rgb_t *im, int x0, int y0, int x1, int y1, int thick, const uint8_t c[3]) {
    const int dx = FX_ABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -FX_ABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    const int t0 = thick / 2;
    for (;;) {
        fx_fill_rect(im, x0 - t0, y0 - t0, thick, thick, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fx_arc(fx_rgb_t *im, int cx, int cy, int r, int thick, float a0, float a1, const uint8_t c[3]) {
    const int n = (int)((a1 - a0) * (float)r) + 1;
    const int t0 = thick / 2;
    for (int i = 0; i <= n; i++) {
        const float a = a0 + (a1 - a0) * (float)i / (float)n;
        const int x = cx + (int)lroundf(cosf(a) * (float)r);
        const int y = cy + (int)lroundf(sinf(a) * (float)r);
        fx_fill_rect(im, x - t0, y - t0, thick, thick, c);
    }
}

int fx_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

void fx_text(fx_rgb_t *im, int x, int y, int scale, const char *s, const uint8_t c[3]) {
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *gl = fx_font5x7[(uint8_t)*s & 0x7F];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (gl[row] & (0x10 >> col))
                    fx_fill_rect(im, x + col * scale, y + row * scale, scale, scale, c);
    }
}

void fx_m_xor(uint8_t *m, int x, int y) {
    if ((unsigned)x < FX_PW && (unsigned)y < FX_PH) m[y * FX_PW + x] ^= 1;
}

void fx_m_set(uint8_t *m, int x, int y, uint8_t v) {
    if ((unsigned)x < FX_PW && (unsigned)y < FX_PH) m[y * FX_PW + x] = v;
}
