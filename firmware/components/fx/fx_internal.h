// Общие помощники ядра эффектов. Наружу не экспортируются.
#pragma once
#include "fx.h"

#include <stdlib.h>
#include <string.h>

#define FX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FX_MAX(a, b) ((a) > (b) ? (a) : (b))
#define FX_ABS(a) ((a) < 0 ? -(a) : (a))

static inline int fx_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline uint8_t fx_u8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Яркость по BT.601 в целых числах: 77+150+29 = 256.
static inline uint8_t fx_luma(const uint8_t *p) {
    return (uint8_t)((77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8);
}

// xorshift32: быстрый и предсказуемый генератор, один и тот же seed даёт
// один и тот же эффект на ПК и на камере.
static inline uint32_t fx_rand(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline int fx_rand_range(uint32_t *s, int lo, int hi) {
    return lo + (int)(fx_rand(s) % (uint32_t)(hi - lo + 1));
}

// Пороговые матрицы упорядоченного дизеринга, значения 0..255.
extern const uint8_t fx_bayer4[4][4];
extern const uint8_t fx_bayer8[8][8];

// ---- операции над плоскостями (fx_core.c)
void fx_luma_plane(const fx_rgb_t *im, uint8_t *L);
// Размытие окном (2r+1); проходы по строкам, без чтения столбцами из PSRAM.
void fx_blur_plane(uint8_t *p, int w, int h, int ch, int r);
void fx_sobel(const uint8_t *L, uint8_t *M, int w, int h);
// Процентили гистограммы (в десятых долях процента).
void fx_percentiles(const uint8_t *L, int n, int lo_x10, int hi_x10, int *lo, int *hi);
void fx_levels_lut(uint8_t lut[256], int lo, int hi);
void fx_progress(fx_job_t *j, int pct);

// ---- рисование на RGB (оверлеи)
void fx_fill_rect(fx_rgb_t *im, int x, int y, int w, int h, const uint8_t c[3]);
void fx_line(fx_rgb_t *im, int x0, int y0, int x1, int y1, int thick, const uint8_t c[3]);
void fx_arc(fx_rgb_t *im, int cx, int cy, int r, int thick, float a0, float a1, const uint8_t c[3]);
void fx_text(fx_rgb_t *im, int x, int y, int scale, const char *s, const uint8_t c[3]);
int fx_text_width(const char *s, int scale);

// ---- рисование на маске превью 0/1 (FX_PW x FX_PH)
void fx_m_xor(uint8_t *m, int x, int y);
void fx_m_set(uint8_t *m, int x, int y, uint8_t v);

// ---- инструменты (fx_tools.c)
typedef struct {
    uint8_t v;
    uint8_t c[3];
} fx_stop_t;

void fx_down2_rgb(const fx_rgb_t *src, uint8_t *dst);
void fx_up2_rgb(const uint8_t *src, int sw, int sh, fx_rgb_t *dst);
void fx_kuwahara(uint8_t *H, int w, int h, int rq, uint8_t *M, uint8_t *mL, uint8_t *mL2);
void fx_xdog(const fx_rgb_t *im, uint8_t *A, uint8_t *B, int r1, int r2, int t);
void fx_mask_clean(uint8_t *m, int w, int h, int keep, int grow);
void fx_bits_pack(const uint8_t *m, int n, uint8_t *bits);
#define FX_BIT(bits, i) (((bits)[(i) >> 3] >> ((i) & 7)) & 1)
void fx_bloom(fx_rgb_t *im, int thr, int gain_q8, int radius, uint8_t *tmp);
void fx_ca_channel(fx_rgb_t *im, int ch, float s, uint8_t *plane);
void fx_swirl(fx_rgb_t *im, int cx, int cy, int R, float amount, uint8_t *tmp, size_t tmp_size);
void fx_grain(fx_rgb_t *im, uint32_t seed, int amount, int vignette);
void fx_gradient_lut(uint8_t lut[256][3], const fx_stop_t *st, int n);
void fx_find_hotspot(const fx_rgb_t *im, int *ox, int *oy);
int fx_find_horizon(const uint8_t *L, int w, int h);
void fx_blend_rect(fx_rgb_t *im, int x, int y, int w, int h, const uint8_t c[3], int a);
void fx_frame_rect(fx_rgb_t *im, int x, int y, int w, int h, int t, const uint8_t c[3]);
void fx_text_outlined(fx_rgb_t *im, int x, int y, int scale, const char *s, const uint8_t fg[3], const uint8_t ol[3]);
void fx_shot_label(char *buf, uint32_t shot);

// Тон 0..255 по кругу цветов (0 = красный, 85 = зелёный, 170 = синий).
static inline int fx_hue8(int r, int g, int b, int *sat) {
    const int mx = FX_MAX(r, FX_MAX(g, b)), mn = FX_MIN(r, FX_MIN(g, b)), d = mx - mn;
    if (sat) *sat = d;
    if (d == 0) return 0;
    int h;
    if (mx == r)
        h = 43 * (g - b) / d;
    else if (mx == g)
        h = 85 + 43 * (b - r) / d;
    else
        h = 171 + 43 * (r - g) / d;
    return h & 255;
}

// Синий шум 64x64 (tools/gen_bluenoise.py): пороги без узора и без комков.
extern const uint8_t fx_bluenoise[64][64];

// ---- стили
void fx_neon_render(fx_job_t *j);
void fx_y2k_render(fx_job_t *j);
void fx_melt_render(fx_job_t *j);
void fx_ghost_render(fx_job_t *j);
void fx_mosh_render(fx_job_t *j);
void fx_dmg_render(fx_job_t *j);
void fx_ink_render(fx_job_t *j);
void fx_tide_render(fx_job_t *j);
void fx_pulse_render(fx_job_t *j);
void fx_shard_render(fx_job_t *j);
void fx_holo_render(fx_job_t *j);
void fx_loop_render(fx_job_t *j);
void fx_topo_render(fx_job_t *j);

// превью новых стилей (fx_preview2.c)
void fx_prev_tide(fx_prev_t *st, uint8_t *g, uint8_t *m);
void fx_prev_pulse(fx_prev_t *st, uint8_t *g, uint8_t *m);
void fx_prev_shard(fx_prev_t *st, uint8_t *g, uint8_t *m);
void fx_prev_holo(fx_prev_t *st, uint8_t *g, uint8_t *m);
void fx_prev_loop(fx_prev_t *st, uint8_t *g, uint8_t *m);
void fx_prev_topo(fx_prev_t *st, uint8_t *g, uint8_t *m);

// Точка с прозрачностью a (0..256) и сложение «светом» (для свечения линий).
static inline void fx_plot(fx_rgb_t *im, int x, int y, const uint8_t c[3], int a) {
    if ((unsigned)x >= (unsigned)im->w || (unsigned)y >= (unsigned)im->h) return;
    uint8_t *d = im->p + ((size_t)y * im->w + x) * 3;
    for (int k = 0; k < 3; k++) d[k] = (uint8_t)((d[k] * (256 - a) + c[k] * a) >> 8);
}
static inline void fx_plot_add(fx_rgb_t *im, int x, int y, const uint8_t c[3], int a) {
    if ((unsigned)x >= (unsigned)im->w || (unsigned)y >= (unsigned)im->h) return;
    uint8_t *d = im->p + ((size_t)y * im->w + x) * 3;
    for (int k = 0; k < 3; k++) d[k] = fx_u8(d[k] + ((c[k] * a) >> 8));
}
