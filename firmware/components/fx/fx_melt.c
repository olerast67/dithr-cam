// MELT: пиксель-сортинг. Кадр делится на отрезки по маске яркости (как в
// ASDFPixelSort Кима Асендорфа), и внутри каждого отрезка пиксели
// сортируются. Маска строится по размытой яркости и короткие отрезки
// отбрасываются: иначе вместо «стекания» получается шум.
//  DRIP   столбцы, светлое стекает вниз
//  RISE   столбцы, светлое поднимается
//  WIND   строки, ветер сдувает вбок
//  MOLTEN огненная карта цвета, потёки и свечение
#include "fx_internal.h"

enum { V_DRIP, V_RISE, V_WIND, V_MOLTEN };

typedef struct {
    fx_job_t *j;
    const uint8_t *mask;  // размытая яркость
    int lo, hi;
    int vertical, descending;
    int min_run, max_run;
} melt_ctx_t;

// Сортировка подсчётом по ключу 0..255: O(n) на отрезок.
static void sort_run(uint8_t *p0, size_t stride, const uint8_t *key, int n, int desc, uint8_t *tmp, uint16_t *cnt) {
    memset(cnt, 0, 257 * sizeof(uint16_t));
    for (int i = 0; i < n; i++) cnt[(desc ? 255 - key[i] : key[i]) + 1]++;
    for (int v = 0; v < 256; v++) cnt[v + 1] += cnt[v];
    for (int i = 0; i < n; i++) {
        const uint8_t *p = p0 + (size_t)i * stride;
        uint8_t *t = tmp + (size_t)cnt[desc ? 255 - key[i] : key[i]]++ * 3;
        t[0] = p[0];
        t[1] = p[1];
        t[2] = p[2];
    }
    for (int i = 0; i < n; i++) {
        uint8_t *p = p0 + (size_t)i * stride;
        p[0] = tmp[i * 3];
        p[1] = tmp[i * 3 + 1];
        p[2] = tmp[i * 3 + 2];
    }
}

// Одна линия (столбец или строка) длиной len с шагом stride.
static void melt_line(melt_ctx_t *k, int line, uint8_t *tmp, uint8_t *key, uint16_t *cnt) {
    const int w = k->j->img.w, h = k->j->img.h;
    const int len = k->vertical ? h : w;
    const size_t stride = k->vertical ? (size_t)w * 3 : 3;
    const size_t mstride = k->vertical ? (size_t)w : 1;
    uint8_t *base = k->vertical ? k->j->img.p + (size_t)line * 3 : k->j->img.p + (size_t)line * w * 3;
    const uint8_t *mb = k->vertical ? k->mask + line : k->mask + (size_t)line * w;
    uint32_t rng = k->j->seed ^ (uint32_t)(line * 0x85EBCA6Bu) ^ (uint32_t)k->vertical;
    fx_rand(&rng);
    int i = 0;
    while (i < len) {
        const int m = mb[(size_t)i * mstride];
        if (m < k->lo || m > k->hi) {
            i++;
            continue;
        }
        const int cap = fx_rand_range(&rng, k->min_run * 2, k->max_run);
        int n = 0;
        while (i + n < len && n < cap) {
            const int v = mb[(size_t)(i + n) * mstride];
            if (v < k->lo || v > k->hi) break;
            n++;
        }
        if (n >= k->min_run) {
            uint8_t *p0 = base + (size_t)i * stride;
            for (int q = 0; q < n; q++) key[q] = fx_luma(p0 + (size_t)q * stride);
            sort_run(p0, stride, key, n, k->descending, tmp, cnt);
        }
        i += n;
    }
}

static void melt_lines(void *c, int a, int b) {
    melt_ctx_t *k = c;
    const int len = FX_MAX(k->j->img.w, k->j->img.h);
    uint8_t *tmp = malloc((size_t)len * 3);
    uint8_t *key = malloc((size_t)len);
    uint16_t *cnt = malloc(257 * sizeof(uint16_t));
    if (tmp && key && cnt)
        for (int l = a; l < b; l++) melt_line(k, l, tmp, key, cnt);
    free(tmp);
    free(key);
    free(cnt);
}

typedef struct {
    fx_job_t *j;
    uint8_t lut[256][3];
    int mode;  // 0: насыщенность x1.3, 1: огненная карта
} col_ctx_t;

static void color_rows(void *c, int y0, int y1) {
    col_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            const int l = fx_luma(d);
            if (k->mode == 1) {
                memcpy(d, k->lut[l], 3);
            } else {
                for (int ch = 0; ch < 3; ch++) d[ch] = fx_u8(l + ((d[ch] - l) * 21 >> 4));
            }
        }
    }
}

void fx_melt_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h, n = w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    const int v = j->variant;

    col_ctx_t cc = {.j = j, .mode = v == V_MOLTEN};
    if (v == V_MOLTEN) {
        static const fx_stop_t FIRE[] = {
            {0, {8, 0, 16}}, {70, {120, 0, 40}}, {130, {240, 60, 10}}, {190, {255, 180, 40}}, {255, {255, 255, 220}},
        };
        fx_gradient_lut(cc.lut, FIRE, 5);
        fx_par_rows(color_rows, &cc, h);
    }

    uint8_t *M = j->scratch;
    fx_luma_plane(&j->img, M);
    fx_blur_plane(M, w, h, 1, 2);
    fx_progress(j, 20);

    melt_ctx_t k = {.j = j, .mask = M};
    // Пороги маски от процентилей кадра со случайным разбросом (по умолчанию
    // в pixelsort это 0.25..0.8 светлоты).
    int p_lo, p_hi;
    fx_percentiles(M, n, fx_rand_range(&rng, 200, 450), fx_rand_range(&rng, 930, 995), &p_lo, &p_hi);
    k.lo = p_lo;
    k.hi = p_hi;
    k.vertical = v != V_WIND;
    k.descending = v == V_RISE || (v == V_WIND && (fx_rand(&rng) & 1));
    k.min_run = FX_MAX(6, h / 50);
    k.max_run = fx_rand_range(&rng, h / 3, h);
    fx_par_rows(melt_lines, &k, k.vertical ? w : h);
    fx_progress(j, 80);

    if (v != V_MOLTEN) {
        fx_par_rows(color_rows, &cc, h);
    } else {
        fx_bloom(&j->img, 170, 700, FX_MAX(3, w / 120), j->scratch);
    }
    fx_grain(&j->img, j->seed, 4, 50);
}
