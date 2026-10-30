// TOPO: кадр как бумажная топографическая карта. Яркость считается высотой:
// сглаживается, режется на уровни, на границах уровней — горизонтали,
// каждая пятая толще. Рельеф подсвечивается сбоку (отмывка), низины
// тонируются зелёным, склоны охрой, вершины почти белые. Поверх сетка
// координат, подписи высот и шапка листа.
#include <stdio.h>

#include "fx_internal.h"

typedef struct {
    fx_job_t *j;
    const uint8_t *Hh;
    int step;
    uint8_t tint[256][3];
} topo_ctx_t;

static const uint8_t LINE[3] = {110, 66, 34};  // коричневые горизонтали

static void topo_rows(void *c, int y0, int y1) {
    topo_ctx_t *k = c;
    const int w = k->j->img.w, h = k->j->img.h;
    for (int y = y0; y < y1; y++) {
        const uint8_t *r = k->Hh + (size_t)y * w;
        const uint8_t *rd = k->Hh + (size_t)(y < h - 1 ? y + 1 : y) * w;
        const uint8_t *ru = k->Hh + (size_t)(y > 0 ? y - 1 : y) * w;
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            const int v = r[x];
            const int xl = x > 0 ? x - 1 : x, xr = x < w - 1 ? x + 1 : x;
            // отмывка: свет с северо-запада
            const int gx = r[xr] - r[xl], gy = rd[x] - ru[x];
            const int shade = fx_clampi(128 - (gx + gy) * 6, 0, 255);
            const int lv = v / k->step;
            const int contour = (rd[x] / k->step != lv) || (r[xr] / k->step != lv);
            const int index = contour && (lv % 5 == 0);
            const uint8_t *t = k->tint[v];
            int col[3];
            for (int ch = 0; ch < 3; ch++) col[ch] = t[ch] * (shade + 128) >> 8;
            if (contour) {
                const int a = index ? 256 : 170;
                for (int ch = 0; ch < 3; ch++) col[ch] = (col[ch] * (256 - a) + LINE[ch] * a) >> 8;
            }
            d[0] = fx_u8(col[0]);
            d[1] = fx_u8(col[1]);
            d[2] = fx_u8(col[2]);
        }
    }
}

// Индексные горизонтали (каждая пятая) утолщаются на пиксель.
static void thicken_index(fx_job_t *j, const uint8_t *Hh, int step) {
    const int w = j->img.w, h = j->img.h;
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
            const int lv = Hh[(size_t)y * w + x] / step;
            if (lv % 5 || Hh[(size_t)(y + 1) * w + x] / step == lv) continue;
            fx_plot(&j->img, x, y - 1, LINE, 256);
            fx_plot(&j->img, x + 1, y, LINE, 256);
        }
}

void fx_topo_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h, n = w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);

    uint8_t *Hh = j->scratch;
    fx_luma_plane(im, Hh);
    int lo, hi;
    fx_percentiles(Hh, n, 10, 990, &lo, &hi);
    uint8_t lut[256];
    fx_levels_lut(lut, lo, hi);
    for (int i = 0; i < n; i++) Hh[i] = lut[Hh[i]];
    for (int i = 0; i < 3; i++) fx_blur_plane(Hh, w, h, 1, FX_MAX(2, w / 160));
    fx_progress(j, 35);

    topo_ctx_t k = {.j = j, .Hh = Hh, .step = fx_rand_range(&rng, 10, 16)};
    static const fx_stop_t T[] = {{0, {96, 140, 110}}, {70, {150, 184, 120}}, {140, {214, 200, 140}},
                                  {200, {196, 150, 104}}, {255, {245, 240, 232}}};
    fx_gradient_lut(k.tint, T, 5);
    fx_par_rows(topo_rows, &k, h);
    thicken_index(j, Hh, k.step);
    fx_progress(j, 75);

    // Сетка координат и подписи высот на индексных горизонталях
    static const uint8_t GRID[3] = {60, 90, 110}, TC[3] = {90, 50, 24}, BG[3] = {236, 228, 206};
    for (int x = w / 8; x < w; x += w / 8) fx_blend_rect(im, x, 0, 1, h, GRID, 140);
    for (int y = h / 6; y < h; y += h / 6) fx_blend_rect(im, 0, y, w, 1, GRID, 140);
    const int sc = FX_MAX(1, w / 400);
    for (int i = 0, tries = 0; i < 9 && tries < 3000; tries++) {
        const int x = fx_rand_range(&rng, w / 12, w - w / 6), y = fx_rand_range(&rng, h / 12, h - h / 10);
        const int v = Hh[(size_t)y * w + x];
        if ((v / k.step) % 5 || Hh[(size_t)(y + 1) * w + x] / k.step == v / k.step) continue;
        char b[12];
        snprintf(b, sizeof(b), "%d", 100 + v * 9);
        fx_fill_rect(im, x - 2, y - 4 * sc, fx_text_width(b, sc) + 4, 9 * sc, BG);
        fx_text(im, x, y - 3 * sc, sc, b, TC);
        i++;
    }
    // Шапка листа
    char hdr[40], shot[8];
    fx_shot_label(shot, j->shot);
    snprintf(hdr, sizeof(hdr), "SHEET %s   SCALE 1:2500", shot);
    const int hs = sc + 1;
    fx_fill_rect(im, w / 40, h / 40, fx_text_width(hdr, hs) + 12, 7 * hs + 10, BG);
    fx_text(im, w / 40 + 6, h / 40 + 5, hs, hdr, TC);
    fx_grain(im, j->seed, 7, 30);
}
