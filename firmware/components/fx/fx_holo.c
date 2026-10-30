// HOLO: кадр становится объёмным. Яркость — высота, сетка точек кадра
// поднимается рельефом, наклоняется и поворачивается, затем рисуется с
// перспективой на чёрном, как проекция голограммы или скан лидара.
//  CLOUD  облако точек в цветах снимка, свечение
//  WIRE   каркасная сетка: голубые и пурпурные линии по высоте
//  SCAN   только строки скана: светящиеся горизонтальные профили, HUD
#include <math.h>
#include <stdio.h>

#include "fx_internal.h"

enum { V_CLOUD, V_WIRE, V_SCAN };

typedef struct {
    float ca, sa, cb, sb;  // наклон (вокруг X) и поворот (вокруг Y)
    float f, cx, cy, zs, scale;
} cam_t;

// Точка кадра (x, y, высота 0..255) -> экран. 0, если позади камеры.
static inline int project(const cam_t *c, float x, float y, float hgt, int *sx, int *sy) {
    float X = (x - c->cx) * c->scale, Y = (y - c->cy) * c->scale, Z = -hgt * c->zs;
    // поворот вокруг Y
    const float X1 = X * c->cb + Z * c->sb, Z1 = -X * c->sb + Z * c->cb;
    // наклон вокруг X
    const float Y2 = Y * c->ca - Z1 * c->sa, Z2 = Y * c->sa + Z1 * c->ca;
    const float d = c->f + Z2;
    if (d < 50.0f) return 0;
    *sx = (int)(c->cx + X1 * c->f / d);
    *sy = (int)(c->cy * 1.05f + Y2 * c->f / d);
    return 1;
}

void fx_holo_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h;
    const int w2 = w / 2, h2 = h / 2;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    const int v = j->variant % 3;

    uint8_t *C = j->scratch;                             // половинный RGB
    uint8_t *H = j->scratch + (size_t)w2 * h2 * 3;       // высота
    fx_down2_rgb(im, C);
    for (int i = 0; i < w2 * h2; i++) H[i] = fx_luma(C + (size_t)i * 3);
    fx_blur_plane(H, w2, h2, 1, 2);
    int lo, hi;
    fx_percentiles(H, w2 * h2, 20, 990, &lo, &hi);
    uint8_t lut[256];
    fx_levels_lut(lut, lo, hi);
    for (int i = 0; i < w2 * h2; i++) H[i] = lut[H[i]];
    fx_progress(j, 20);

    static const uint8_t BG[3] = {2, 3, 10};
    fx_fill_rect(im, 0, 0, w, h, BG);
    const float tilt = (float)fx_rand_range(&rng, 30, 44) * 0.01745f;
    const float yaw = (float)fx_rand_range(&rng, -28, 28) * 0.01745f;
    cam_t cam = {cosf(tilt), sinf(tilt), cosf(yaw), sinf(yaw), (float)w * 1.1f, (float)w / 2, (float)h / 2,
                 (float)h * 0.32f / 255.0f, 0.86f};

    // Пол: перспективная сетка под рельефом
    static const uint8_t GRID[3] = {20, 40, 90};
    for (int gx = 0; gx <= 10; gx++) {
        int x0, y0, x1, y1;
        const float xx = (float)gx * (float)w / 10.0f;
        if (project(&cam, xx, 0, 0, &x0, &y0) && project(&cam, xx, (float)h, 0, &x1, &y1)) fx_line(im, x0, y0, x1, y1, 1, GRID);
    }
    for (int gy = 0; gy <= 8; gy++) {
        int x0, y0, x1, y1;
        const float yy = (float)gy * (float)h / 8.0f;
        if (project(&cam, 0, yy, 0, &x0, &y0) && project(&cam, (float)w, yy, 0, &x1, &y1)) fx_line(im, x0, y0, x1, y1, 1, GRID);
    }

    // Рисуем от дальних строк к ближним (после наклона дальние — верхние)
    const int step = v == V_CLOUD ? 3 : (v == V_WIRE ? 10 : 6);
    for (int y = 0; y < h; y += step) {
        int px = -1, py = -1;
        for (int x = 0; x < w; x += (v == V_CLOUD ? step : 2)) {
            const int hx = FX_MIN(x / 2, w2 - 1), hy = FX_MIN(y / 2, h2 - 1);
            const float hv = (float)H[hy * w2 + hx];
            int sx, sy;
            if (!project(&cam, (float)x, (float)y, hv, &sx, &sy)) {
                px = -1;
                continue;
            }
            const uint8_t *c = C + ((size_t)hy * w2 + hx) * 3;
            uint8_t col[3];
            if (v == V_CLOUD) {
                const int l = fx_luma(c);
                for (int k = 0; k < 3; k++) col[k] = fx_u8(l + (c[k] - l) * 2 + 30);
                fx_plot_add(im, sx, sy, col, 230);
                fx_plot_add(im, sx + 1, sy, col, 110);
                fx_plot_add(im, sx, sy + 1, col, 110);
            } else {
                // цвет по высоте: низ голубой, верх пурпурный
                const int t = (int)hv;
                col[0] = (uint8_t)(40 + t * 215 / 255);
                col[1] = (uint8_t)(230 - t * 170 / 255);
                col[2] = 255;
                if (v == V_SCAN) {
                    const int b = 90 + t * 165 / 255;
                    col[0] = (uint8_t)(b / 4);
                    col[1] = (uint8_t)b;
                    col[2] = 255;
                }
                if (px >= 0) fx_line(im, px, py, sx, sy, 1, col);
            }
            px = sx;
            py = sy;
        }
        if ((y & 31) == 0) fx_progress(j, 20 + y * 60 / h);
    }
    // У сетки ещё и вертикальные нити
    if (v == V_WIRE)
        for (int x = 0; x < w; x += 10) {
            int px = -1, py = -1;
            for (int y = 0; y < h; y += 4) {
                const int hx = FX_MIN(x / 2, w2 - 1), hy = FX_MIN(y / 2, h2 - 1);
                int sx, sy;
                if (!project(&cam, (float)x, (float)y, (float)H[hy * w2 + hx], &sx, &sy)) continue;
                static const uint8_t VC[3] = {40, 120, 200};
                if (px >= 0) fx_line(im, px, py, sx, sy, 1, VC);
                px = sx;
                py = sy;
            }
        }

    fx_bloom(im, 60, 800, FX_MAX(2, w / 180), j->scratch);
    // Строки развёртки проектора и подпись
    for (int y = 0; y < h; y += 3) {
        uint8_t *row = im->p + (size_t)y * w * 3;
        for (int x = 0; x < w * 3; x++) row[x] = (uint8_t)(row[x] * 200 >> 8);
    }
    char b[40], shot[8];
    fx_shot_label(shot, j->shot);
    snprintf(b, sizeof(b), "VOLUMETRIC CAPTURE %s", shot);
    static const uint8_t TC[3] = {120, 220, 255};
    const int sc = FX_MAX(1, w / 400);
    fx_text(im, w / 30, h / 30, sc * 2, b, TC);
    snprintf(b, sizeof(b), "TILT %d  YAW %+d  PTS %d", (int)(tilt * 57.3f), (int)(yaw * 57.3f), (w / step) * (h / step));
    fx_text(im, w / 30, h - h / 30 - 7 * sc, sc, b, TC);
    fx_grain(im, j->seed, 5, 60);
}
