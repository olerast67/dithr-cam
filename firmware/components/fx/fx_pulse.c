// PULSE: кадр рисуется сотней горизонтальных линий, каждую приподнимает
// яркость под ней, как на обложке Unknown Pleasures или на экране
// осциллографа. Линии идут от дальних (вверху) к ближним: каждая следующая
// закрашивает под собой фон и прячет то, что за ней. Отсюда ощущение рельефа.
//  SCOPE   зелёный люминофор на чёрном, свечение, сетка осциллографа
//  CHROMA  линии в цветах снимка на чёрном, со свечением
#include "fx_internal.h"

enum { V_SCOPE, V_CHROMA };

typedef struct {
    uint8_t bg[3], fg[3];
    int glow, grid;
} pulse_look_t;

static const pulse_look_t LOOKS[2] = {
    {{4, 10, 6}, {90, 255, 150}, 1, 1},
    {{6, 4, 12}, {255, 255, 255}, 1, 0},
};

void fx_pulse_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h, n = w * h;
    const pulse_look_t *Lk = &LOOKS[j->variant % 2];
    uint32_t rng = j->seed;
    fx_rand(&rng);

    // Высота: сглаженная яркость с уровнями. Цвет снимка для CHROMA:
    // уменьшенная копия (кадр сейчас будет закрашен фоном).
    uint8_t *H = j->scratch;
    uint8_t *C = j->scratch + n;  // половинный RGB: 360 КБ
    fx_luma_plane(im, H);
    fx_blur_plane(H, w, h, 1, 2);
    int lo, hi;
    fx_percentiles(H, n, 20, 990, &lo, &hi);
    uint8_t lut[256];
    fx_levels_lut(lut, lo, hi);
    for (int i = 0; i < n; i++) H[i] = lut[H[i]];
    if (j->variant == V_CHROMA) fx_down2_rgb(im, C);
    fx_progress(j, 25);

    fx_fill_rect(im, 0, 0, w, h, Lk->bg);
    if (Lk->grid) {  // сетка осциллографа
        static const uint8_t G[3] = {16, 46, 26};
        for (int x = 0; x < w; x += w / 10) fx_fill_rect(im, x, 0, 1, h, G);
        for (int y = 0; y < h; y += h / 8) fx_fill_rect(im, 0, y, w, 1, G);
    }

    const int S = fx_rand_range(&rng, h / 90, h / 60);    // шаг строк
    const int A = S * fx_rand_range(&rng, 5, 9);           // высота пиков
    const int t = 2;
    const int margin = w / 16;
    int *ys = malloc(sizeof(int) * (size_t)w);
    if (!ys) {
        j->failed = true;
        return;
    }
    const uint8_t *fill = Lk->bg;
    for (int y0 = A + S; y0 < h - S; y0 += S) {
        // профиль строки: высота по яркости, у краёв гаснет (как на обложке)
        for (int x = 0; x < w; x++) {
            int env = 256;
            if (x < margin) env = x * 256 / margin;
            if (x > w - margin) env = (w - x) * 256 / margin;
            const int v = H[(size_t)y0 * w + x];
            const int jit = (int)(fx_rand(&rng) % 3) - 1;
            ys[x] = y0 - ((v * v / 255) * A / 255) * env / 256 + jit * (v > 40);
        }
        // заслонка: от линии вниз до основания закрашиваем фоном
        for (int x = 0; x < w; x++) {
            const int top = ys[x] + 1;
            for (int y = top; y <= y0 + 1 && y < h; y++) {
                uint8_t *d = im->p + ((size_t)y * w + x) * 3;
                d[0] = fill[0];
                d[1] = fill[1];
                d[2] = fill[2];
            }
        }
        // сама линия: вертикальные отрезки между соседними точками, без дыр
        for (int x = 1; x < w; x++) {
            uint8_t col[3] = {Lk->fg[0], Lk->fg[1], Lk->fg[2]};
            if (j->variant == V_CHROMA) {
                const uint8_t *c = C + ((size_t)FX_MIN(y0 / 2, h / 2 - 1) * (w / 2) + FX_MIN(x / 2, w / 2 - 1)) * 3;
                const int l = fx_luma(c);
                for (int k = 0; k < 3; k++) col[k] = fx_u8(l + ((c[k] - l) * 2) + 40);
            } else if (j->variant == V_SCOPE) {
                // выше пик — ярче луч
                const int b = 140 + (y0 - ys[x]) * 115 / FX_MAX(A, 1);
                for (int k = 0; k < 3; k++) col[k] = (uint8_t)(Lk->fg[k] * FX_MIN(b, 255) >> 8);
            }
            const int a = FX_MIN(ys[x - 1], ys[x]), b = FX_MAX(ys[x - 1], ys[x]);
            fx_fill_rect(im, x, a, 1, b - a + t, col);
        }
    }
    free(ys);
    fx_progress(j, 80);
    if (Lk->glow) fx_bloom(im, 60, 900, FX_MAX(2, w / 200), j->scratch);
    fx_grain(im, j->seed, 5, 80);
}
