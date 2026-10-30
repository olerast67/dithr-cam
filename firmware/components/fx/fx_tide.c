// TIDE: снимок из времени. Камера снимает FX_TIDE_FRAMES кадров подряд
// (несколько секунд), и каждый пиксель берётся из своего момента по карте
// времени T. Неподвижное остаётся как есть, а всё, что двигалось, тянется,
// гнётся и течёт по форме карты.
//  SLIT    щелевая развёртка: время бежит поперёк кадра (как фотофиниш)
//  RADIAL  время расходится кольцами от центра
//  LIQUID  время течёт волнами: кадр как поверхность воды
#include <math.h>

#include "fx_internal.h"

enum { V_SLIT, V_RADIAL, V_LIQUID };

void fx_tide_map(int variant, uint32_t seed, uint8_t *T, int w, int h) {
    const int K = FX_TIDE_FRAMES;
    uint32_t rng = seed ^ 0x71DE71DEu;
    fx_rand(&rng);
    switch (variant) {
        case V_RADIAL: {
            const float cx = (float)w / 2, cy = (float)h / 2;
            const float rmax = sqrtf(cx * cx + cy * cy);
            const int inward = (int)(fx_rand(&rng) & 1);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy)) / rmax;
                    int t = (int)(d * (float)K);
                    if (t >= K) t = K - 1;
                    T[(size_t)y * w + x] = (uint8_t)(inward ? K - 1 - t : t);
                }
            break;
        }
        case V_LIQUID: {
            float fx[3], fy[3], ph[3];
            for (int i = 0; i < 3; i++) {
                fx[i] = (float)fx_rand_range(&rng, 2, 9) / (float)w * 6.2832f / 2;
                fy[i] = (float)fx_rand_range(&rng, 2, 9) / (float)h * 6.2832f / 2;
                ph[i] = (float)(fx_rand(&rng) % 628) / 100.0f;
            }
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    // складываем три волны, вторая искривлена первой: «жидкая» форма
                    const float a = sinf((float)x * fx[0] + ph[0] + sinf((float)y * fy[0]));
                    const float b = sinf((float)y * fy[1] + ph[1] + 1.5f * a);
                    const float c = sinf(((float)x * fx[2] + (float)y * fy[2]) + ph[2]);
                    const float v = (a + b + c) / 6.0f + 0.5f;  // 0..1
                    T[(size_t)y * w + x] = (uint8_t)fx_clampi((int)(v * (float)K), 0, K - 1);
                }
            break;
        }
        default: {
            const int dir = (int)(fx_rand(&rng) % 3);  // 0: слева направо, 1: сверху вниз, 2: по диагонали
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const int t = dir == 0 ? x * K / w : (dir == 1 ? y * K / h : (x * h + y * w) * K / (2 * w * h));
                    T[(size_t)y * w + x] = (uint8_t)FX_MIN(t, K - 1);
                }
            break;
        }
    }
}

typedef struct {
    fx_job_t *j;
    uint8_t lut[256];
} tide_ctx_t;

static void tide_rows(void *c, int y0, int y1) {
    tide_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            const int l = fx_luma(d);
            // чуть плёночной насыщенности и контраста
            for (int ch = 0; ch < 3; ch++) d[ch] = k->lut[fx_u8(l + (d[ch] - l) * 5 / 4)];
        }
    }
}

void fx_tide_render(fx_job_t *j) {
    // Кадры уже сложены по карте времени при съёмке; здесь только доводка.
    tide_ctx_t k = {.j = j};
    uint8_t *L = j->scratch + (size_t)j->img.w * j->img.h;  // карта T занимает первую плоскость
    fx_luma_plane(&j->img, L);
    int lo, hi;
    fx_percentiles(L, j->img.w * j->img.h, 8, 994, &lo, &hi);
    fx_levels_lut(k.lut, lo, hi);
    fx_par_rows(tide_rows, &k, j->img.h);
    fx_grain(&j->img, j->seed, 5, 60);
}
