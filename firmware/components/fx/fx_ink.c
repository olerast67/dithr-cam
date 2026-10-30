// INK: печатные техники, результат без потерь в PNG 1-2 бита.
//  ENGRV  гравюра: волнистые линии, толщина по тону, в тенях второй слой
//  RISO   ризограф в две краски (розовая и синяя), слои сдвинуты, зерно

#include <math.h>

#include "fx_internal.h"

enum { V_ENGRV, V_RISO };

static const uint8_t PAPER[3] = {242, 236, 222};
static const uint8_t INK[3] = {22, 18, 34};

// Подготовка яркости: уровни и нерезкая маска. L и B — плоскости w*h.
static void prep_luma(fx_job_t *j, uint8_t *L, uint8_t *B, int sharpen_q8, int r) {
    const int n = j->img.w * j->img.h;
    fx_luma_plane(&j->img, L);
    int lo, hi;
    fx_percentiles(L, n, 5, 995, &lo, &hi);
    uint8_t lut[256];
    fx_levels_lut(lut, lo, hi);
    for (int i = 0; i < n; i++) L[i] = lut[L[i]];
    if (sharpen_q8) {
        memcpy(B, L, (size_t)n);
        fx_blur_plane(B, j->img.w, j->img.h, 1, r);
        for (int i = 0; i < n; i++) L[i] = fx_u8(L[i] + (((L[i] - B[i]) * sharpen_q8) >> 8));
    }
}

static void engrave(uint8_t *L, int w, int h, uint32_t *rng) {
    const int P = 6;
    const float lam = (float)fx_rand_range(rng, w / 6, w / 3);
    const float A = (float)fx_rand_range(rng, 4, 12);
    float sx[1024];
    for (int x = 0; x < w && x < 1024; x++) sx[x] = A * sinf(6.2832f * (float)x / lam);
    for (int y = 0; y < h; y++) {
        uint8_t *row = L + (size_t)y * w;
        const float sy = A * 0.7f * sinf(6.2832f * (float)y / (lam * 0.8f));
        for (int x = 0; x < w; x++) {
            const int l = row[x];
            // линии слегка текут вслед за тоном, как у гравёра
            const float v = (float)y + sx[x < 1024 ? x : 1023] + (float)l * 0.03f;
            float f = v / (float)P;
            f -= floorf(f);
            const float dark = 1.0f - (float)l / 255.0f;
            int ink = fabsf(f - 0.5f) < dark * 0.5f;
            if (!ink && l < 80) {  // второй, поперечный слой в тенях
                float g = ((float)x + sy) / (float)P;
                g -= floorf(g);
                ink = fabsf(g - 0.5f) < (80 - l) / 160.0f;
            }
            row[x] = ink ? 0 : 1;
        }
    }
}

// Ризограф: тёплое уходит в розовую краску, холодное в синюю, тёмное в обе.
// Каждый слой дизерится своим смещением синего шума и сдвинут при печати.
static void riso(fx_job_t *j, uint8_t *out, const uint8_t *Lv, uint32_t *rng) {
    const int w = j->img.w, h = j->img.h;
    const int bdx = fx_rand_range(rng, -5, 5), bdy = fx_rand_range(rng, -4, 4);
    const int nofs = fx_rand_range(rng, 0, 63);
    for (int y = 0; y < h; y++) {
        uint8_t *o = out + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int bits = 0;
            for (int layer = 0; layer < 2; layer++) {
                const int sx = layer ? fx_clampi(x - bdx, 0, w - 1) : x;
                const int sy = layer ? fx_clampi(y - bdy, 0, h - 1) : y;
                const uint8_t *p = j->img.p + ((size_t)sy * w + sx) * 3;
                const int dark = 255 - Lv[(size_t)sy * w + sx];
                const int warm = p[0] - p[2];  // -255..255
                int dens = dark * (190 + (layer == 0 ? warm : -warm) * 3 / 4) / 255;
                dens = fx_clampi(dens, 0, 255) * 235 / 255;  // краска ложится не на 100%
                const int t = fx_bluenoise[(y + layer * nofs) & 63][(x + layer * 29) & 63];
                if (dens > t) bits |= 1 << layer;
            }
            if (bits && (fx_rand(rng) & 127) == 0) bits = 0;  // непропечатка
            o[x] = (uint8_t)bits;
        }
    }
}

void fx_ink_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h;
    uint8_t *L = j->scratch, *B = j->scratch + (size_t)w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    j->idx = L;
    j->pal_n = 2;
    j->bpp = 1;
    memcpy(j->pal[0], INK, 3);
    memcpy(j->pal[1], PAPER, 3);

    switch (j->variant) {
        case V_RISO: {
            static const uint8_t RP[4][3] = {{247, 243, 232}, {255, 72, 176}, {0, 120, 191}, {38, 40, 112}};
            prep_luma(j, B, NULL, 0, 0);  // яркость с уровнями в B, результат в L
            riso(j, L, B, &rng);
            memcpy(j->pal, RP, sizeof(RP));
            j->pal_n = 4;
            j->bpp = 2;
            break;
        }
        default:
            prep_luma(j, L, B, 200, 3);
            fx_progress(j, 40);
            engrave(L, w, h, &rng);
            break;
    }
}
