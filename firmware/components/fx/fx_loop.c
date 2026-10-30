// LOOP: эффект Дросте, кадр бесконечно уходит сам в себя.
// Каждая точка экрана z переводится в логарифм: w = ln|z| + i*arg(z).
// Умножение на комплексное число beta закручивает плоскость так, что
// один оборот вокруг центра = переход на уровень глубже (масштаб s).
// Затем точка возвращается exp() и «нормализуется» по уровням: пока она
// вне кадра — уменьшаем в s раз, пока внутри вложенного кадра — увеличиваем.
//  SPIRAL  классическая спираль Эшера
//  VORTEX  два уровня на оборот, тугой водоворот
#include <math.h>

#include "fx_internal.h"

enum { V_SPIRAL, V_VORTEX };

typedef struct {
    fx_job_t *j;
    const uint16_t *src;  // копия кадра в RGB565
    float br, bi;         // beta
    float s, rot;
    float cx, cy, W2, H2;
} loop_ctx_t;

static void loop_rows(void *c, int y0, int y1) {
    loop_ctx_t *k = c;
    const int w = k->j->img.w, h = k->j->img.h;
    const float edge = 0.022f;  // ширина белой рамки вокруг каждого уровня
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            float zx = (float)x - k->cx + 0.5f, zy = (float)y - k->cy + 0.5f;
            const float lr = 0.5f * logf(zx * zx + zy * zy);
            const float th = atan2f(zy, zx) + k->rot;
            const float wr = lr * k->br - th * k->bi;
            const float wi = lr * k->bi + th * k->br;
            const float m = expf(wr);
            float sx = m * cosf(wi), sy = m * sinf(wi);
            for (int it = 0; it < 24 && (fabsf(sx) > k->W2 || fabsf(sy) > k->H2); it++) {
                sx *= k->s;
                sy *= k->s;
            }
            for (int it = 0; it < 24 && fabsf(sx) < k->W2 * k->s && fabsf(sy) < k->H2 * k->s; it++) {
                sx /= k->s;
                sy /= k->s;
            }
            if (fabsf(sx) > k->W2 * (1.0f - edge) || fabsf(sy) > k->H2 * (1.0f - edge * 4.0f / 3.0f)) {
                d[0] = 246;
                d[1] = 244;
                d[2] = 238;  // рамка, как у фотокарточки
                continue;
            }
            const int ix = fx_clampi((int)(sx + k->W2), 0, w - 1), iy = fx_clampi((int)(sy + k->H2), 0, h - 1);
            const uint16_t p = k->src[(size_t)iy * w + ix];
            d[0] = (uint8_t)((p >> 8) & 0xF8);
            d[1] = (uint8_t)((p >> 3) & 0xFC);
            d[2] = (uint8_t)(p << 3);
        }
    }
}

void fx_loop_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h, n = w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    const int v = j->variant % 2;

    // Копия кадра в RGB565 занимает ровно 2n байт scratch
    uint16_t *src = (uint16_t *)j->scratch;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = im->p + (size_t)i * 3;
        src[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
    }
    fx_progress(j, 15);

    loop_ctx_t k = {.j = j, .src = src, .W2 = (float)w / 2, .H2 = (float)h / 2};
    k.s = v == V_VORTEX ? 0.36f : (float)fx_rand_range(&rng, 38, 50) / 100.0f;
    const float ln_s = logf(k.s);
    const float arms = v == V_VORTEX ? 2.0f : 1.0f;
    k.br = 1.0f;
    k.bi = -arms * ln_s / 6.2832f;
    k.rot = (float)(fx_rand(&rng) % 628) / 100.0f;
    // центр спирали чуть смещён от середины: так интереснее композиция
    k.cx = (float)w / 2 + (float)fx_rand_range(&rng, -w / 12, w / 12);
    k.cy = (float)h / 2 + (float)fx_rand_range(&rng, -h / 12, h / 12);
    fx_par_rows(loop_rows, &k, h);
    fx_progress(j, 90);
    fx_grain(im, j->seed, 4, 70);
}
