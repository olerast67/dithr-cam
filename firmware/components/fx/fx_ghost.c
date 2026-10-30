// GHOST: три кадра с интервалом. Первый целиком лежит в img, от второго
// канал G (FX_GHOST_G), от третьего канал B (FX_GHOST_B).
//  RGB    затвор Харриса: R из первого кадра, G из второго, B из третьего.
//         Неподвижное остаётся обычного цвета, движение расслаивается радугой.
//  ECHO   неподвижный мир гаснет и выцветает, движение оставляет неоновые
//         следы: голубой след второго кадра, пурпурный третьего.
//  XRAY   чёрный фон, контуры трёх моментов светятся в R, G, B. Где ничего не
//         двигалось, контуры совпадают и складываются в белый.
#include "fx_internal.h"

enum { V_RGB, V_ECHO, V_XRAY };

typedef struct {
    fx_job_t *j;
    const uint8_t *G1, *B2;
    uint8_t lut[256];
    int a, b;  // параметры варианта
} ghost_ctx_t;

static void rgb_rows(void *c, int y0, int y1) {
    ghost_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        const uint8_t *g = k->G1 + (size_t)y * w, *b = k->B2 + (size_t)y * w;
        for (int x = 0; x < w; x++, d += 3) {
            d[0] = k->lut[d[0]];
            d[1] = k->lut[g[x]];
            d[2] = k->lut[b[x]];
        }
    }
}

static void echo_rows(void *c, int y0, int y1) {
    ghost_ctx_t *k = c;
    const int w = k->j->img.w;
    static const uint8_t CY[3] = {0, 240, 255}, MG[3] = {255, 40, 210};
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        const uint8_t *g = k->G1 + (size_t)y * w, *b = k->B2 + (size_t)y * w;
        for (int x = 0; x < w; x++, d += 3) {
            // разница с первым кадром по тем же каналам = где было движение
            const int d1 = FX_ABS((int)g[x] - (int)d[1]), d2 = FX_ABS((int)b[x] - (int)d[2]);
            const int m1 = fx_clampi((d1 - k->a) * 4, 0, 255), m2 = fx_clampi((d2 - k->a) * 4, 0, 255);
            const int l = fx_luma(d);
            const int base = l * k->b >> 8;  // выцветший, притушенный мир
            for (int ch = 0; ch < 3; ch++) {
                int v = base + ((d[ch] - l) * k->b >> 9);
                v += ((255 - v) * (CY[ch] * m1 >> 8)) >> 8;
                v += ((255 - v) * (MG[ch] * m2 >> 8)) >> 8;
                d[ch] = fx_u8(v);
            }
        }
    }
}

// Собель по плоскости с произвольным шагом: src и dst могут быть разными
// каналами одной картинки.
typedef struct {
    const uint8_t *src;
    int ss;  // шаг соседних пикселей в src (1 для плоскости, 3 для канала)
    uint8_t *dst;
    int ds;
    int w, h, gain;
} sob_ctx_t;

static void sob_rows(void *c, int y0, int y1) {
    sob_ctx_t *k = c;
    const int w = k->w, h = k->h;
#define P(yy, xx) k->src[((size_t)(yy) * w + (xx)) * k->ss]
    for (int y = y0; y < y1; y++) {
        const int ya = y > 0 ? y - 1 : 0, yb = y < h - 1 ? y + 1 : h - 1;
        for (int x = 0; x < w; x++) {
            const int xa = x > 0 ? x - 1 : 0, xb = x < w - 1 ? x + 1 : w - 1;
            const int gx = P(ya, xb) + 2 * P(y, xb) + P(yb, xb) - P(ya, xa) - 2 * P(y, xa) - P(yb, xa);
            const int gy = P(yb, xa) + 2 * P(yb, x) + P(yb, xb) - P(ya, xa) - 2 * P(ya, x) - P(ya, xb);
            k->dst[((size_t)y * w + x) * k->ds] = fx_u8(((FX_ABS(gx) + FX_ABS(gy)) * k->gain) >> 8);
        }
    }
#undef P
}

static void sobel_into(const uint8_t *src, int ss, uint8_t *dst, int ds, int w, int h, int gain) {
    sob_ctx_t k = {src, ss, dst, ds, w, h, gain};
    fx_par_rows(sob_rows, &k, h);
}

void fx_ghost_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h, n = w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    ghost_ctx_t k = {.j = j, .G1 = FX_GHOST_G(j), .B2 = FX_GHOST_B(j)};
    // Уровни по второму кадру (он уже лежит отдельной плоскостью)
    int lo, hi;
    fx_percentiles(k.G1, n, 5, 995, &lo, &hi);
    fx_levels_lut(k.lut, lo, hi);
    fx_progress(j, 30);

    switch (j->variant) {
        case V_ECHO:
            k.a = fx_rand_range(&rng, 14, 24);
            k.b = fx_rand_range(&rng, 120, 170);
            fx_par_rows(echo_rows, &k, h);
            fx_bloom(&j->img, 150, 600, FX_MAX(3, w / 120), (uint8_t *)k.G1);  // кадры больше не нужны
            fx_grain(&j->img, j->seed, 6, 70);
            break;
        case V_XRAY: {
            // B := яркость первого кадра; R := контур первого; затем B и G из плоскостей
            uint8_t *p = j->img.p;
            for (int i = 0; i < n; i++, p += 3) p[2] = fx_luma(p);
            const int g = fx_rand_range(&rng, 200, 340);
            sobel_into(j->img.p + 2, 3, j->img.p + 0, 3, w, h, g);
            sobel_into(k.B2, 1, j->img.p + 2, 3, w, h, g);
            sobel_into(k.G1, 1, j->img.p + 1, 3, w, h, g);
            fx_bloom(&j->img, 90, 800, FX_MAX(3, w / 140), (uint8_t *)k.G1);
            fx_grain(&j->img, j->seed, 5, 90);
            break;
        }
        default:
            fx_par_rows(rgb_rows, &k, h);
            fx_grain(&j->img, j->seed, 6, 90);
            break;
    }
}
