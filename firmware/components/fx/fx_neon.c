// NEON: кадр как из аниме (варианты VAPOR и MANGA). Конвейер:
//  1. Линии XDoG по полному кадру (тёмная сторона перепадов) -> битовая маска.
//  2. Фильтр Кувахары на половинном размере: листва, волны и кожа
//     сглаживаются в ровные мазки, края остаются резкими.
//  3. Обратно в полный размер, цвет раскладывается по палитре: на каждый
//     оттенок два тона (свет и тень), как в cel-shading.
//  4. Чистка пятен, контур, обводка-«рим» со сдвигом, искры, линии скорости.
//
// Память scratch (n = w*h): [0, 2n) рабочие плоскости, [2n, 2n+n/8) маска линий.
#include <math.h>

#include "fx_internal.h"

enum { I_INK, I_PAPER, I_DEEP, I_RIM, I_MID, I_SPARK, I_FAM0 };
#define I_N (I_FAM0 + 21)  // 7 оттенков x 3 тона: блик, свет, тень
#define TONE_LIGHT 250  // в палитре MANGA: «цвет» = растр
#define TONE_DARK 251

typedef struct {
    uint8_t ink[3], paper[3], deep[3], rim[3], mid[3], spark[3];
    uint8_t fam[7][2][3];  // красный, кожа/оранж, жёлтый, зелёный, бирюза, синий, пурпур; свет и тень
    uint8_t manga;         // 1: тени и середина рисуются растровыми точками
    uint8_t sparkle;       // вероятность искр и звёздочек, 0..255
    uint8_t speed;         // вероятность линий скорости, 0..255
} neon_pal_t;

static const neon_pal_t PALS[] = {
    // VAPOR: пастельный вейпорвейв, мягкие фиолетовые чернила
    {{44, 22, 78}, {255, 242, 250}, {96, 62, 166}, {255, 113, 206}, {148, 208, 255}, {255, 255, 255},
     {{{255, 113, 206}, {185, 80, 170}}, {{255, 190, 200}, {220, 120, 170}}, {{255, 251, 150}, {230, 170, 120}},
      {{5, 255, 161}, {60, 160, 170}}, {{1, 205, 254}, {70, 120, 220}}, {{130, 140, 255}, {80, 60, 180}},
      {{185, 103, 255}, {110, 60, 170}}},
     0, 220, 0},
    // MANGA: чёрное, белое, растр и один красный акцент
    {{0, 0, 0}, {255, 255, 255}, {TONE_DARK, 0, 0}, {0, 0, 0}, {TONE_LIGHT, 0, 0}, {255, 255, 255},
     {{{230, 20, 40}, {150, 0, 20}}, {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}}, {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}},
      {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}}, {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}},
      {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}}, {{TONE_LIGHT, 0, 0}, {TONE_DARK, 0, 0}}},
     1, 0, 160},
};

typedef struct {
    fx_job_t *j;
    const neon_pal_t *P;
    uint8_t col[I_N][3];
    uint8_t *A, *B;
    const uint8_t *bits;
    int shade_t, hi_t, rdx, rdy;
} neon_ctx_t;

static inline uint8_t neon_map(const neon_ctx_t *k, int r, int g, int b) {
    int sat;
    const int h = fx_hue8(r, g, b, &sat);
    const int L = (77 * r + 150 * g + 29 * b) >> 8;
    if (L < 44) return I_INK;
    if (L > 228 && sat < 90) return I_PAPER;
    if (sat < 26 + (L >> 4)) {
        if (L < 105) return I_DEEP;
        if (L < 178) return I_MID;
        return I_PAPER;
    }
    int fam;
    if (h < 10 || h >= 238) fam = 0;
    else if (h < 32) fam = 1;
    else if (h < 50) fam = 2;
    else if (h < 113) fam = 3;
    else if (h < 142) fam = 4;
    else if (h < 181) fam = 5;
    else fam = 6;
    if (L > 205 && sat < 70) return I_PAPER;
    const int tone = L >= k->hi_t ? 0 : (L >= k->shade_t ? 1 : 2);
    return (uint8_t)(I_FAM0 + fam * 3 + tone);
}

static void map_rows(void *c, int y0, int y1) {
    neon_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        const uint8_t *s = k->j->img.p + (size_t)y * w * 3;
        uint8_t *o = k->A + (size_t)y * w;
        for (int x = 0; x < w; x++, s += 3) o[x] = neon_map(k, s[0], s[1], s[2]);
    }
}

// Самый частый индекс в окне 3x3: убирает одиночные точки и рваные края.
static void mode_rows(void *c, int y0, int y1) {
    neon_ctx_t *k = c;
    const int w = k->j->img.w, h = k->j->img.h;
    for (int y = y0; y < y1; y++) {
        const uint8_t *rows[3] = {k->A + (size_t)(y > 0 ? y - 1 : 0) * w, k->A + (size_t)y * w,
                                  k->A + (size_t)(y < h - 1 ? y + 1 : h - 1) * w};
        uint8_t *o = k->B + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const int xs[3] = {x > 0 ? x - 1 : 0, x, x < w - 1 ? x + 1 : w - 1};
            const uint8_t ctr = rows[1][x];
            int same = 0;
            for (int r = 0; r < 3; r++)
                for (int q = 0; q < 3; q++) same += rows[r][xs[q]] == ctr;
            uint8_t idx = ctr;
            if (same < 4) {
                uint8_t cnt[I_N] = {0};
                int best = 0;
                for (int r = 0; r < 3; r++)
                    for (int q = 0; q < 3; q++) {
                        const uint8_t v = rows[r][xs[q]];
                        if (++cnt[v] > best) {
                            best = cnt[v];
                            idx = v;
                        }
                    }
            }
            o[x] = idx;
        }
    }
}

// Растр манги: точки на решётке под 45°, радиус задаёт плотность тона.
static inline int screentone(int x, int y, int dark) {
    const int P = 7;
    const int a = (x + y) % P - P / 2, b = ((x - y) % P + P) % P - P / 2;
    return a * a + b * b < (dark ? 9 : 3);
}

static void compose_rows(void *c, int y0, int y1) {
    neon_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        uint32_t rng = k->j->seed ^ (uint32_t)(y * 2654435761u) ^ 0xA5A5A5A5u;
        fx_rand(&rng);
        const uint8_t *s = k->B + (size_t)y * w;
        const int yr = y - k->rdy;
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            const int i = y * w + x;
            const uint8_t v = s[x];
            const uint8_t *col = k->col[v];
            const int xr = x - k->rdx;
            if (FX_BIT(k->bits, i)) {
                col = k->col[I_INK];
            } else if (v != I_INK && yr >= 0 && yr < k->j->img.h && xr >= 0 && xr < w &&
                       FX_BIT(k->bits, yr * w + xr) && !k->P->manga) {
                col = k->col[I_RIM];
            } else if (col[0] == TONE_LIGHT || col[0] == TONE_DARK) {
                col = screentone(x, y, col[0] == TONE_DARK) ? k->col[I_INK] : k->col[I_PAPER];
            } else if (v == I_MID && k->P->sparkle && (fx_rand(&rng) & 0x3FF) < 3) {
                col = k->col[I_SPARK];  // мелкие искры на фоне
            }
            d[0] = col[0];
            d[1] = col[1];
            d[2] = col[2];
        }
    }
}

// Звёздочки-блики на самых светлых местах.
static void sparkles(fx_job_t *j, const uint8_t c[3], uint32_t *rng) {
    fx_rgb_t *im = &j->img;
    const int n = fx_rand_range(rng, 6, 16);
    for (int i = 0, tries = 0; i < n && tries < 400; tries++) {
        const int x = fx_rand_range(rng, 8, im->w - 9), y = fx_rand_range(rng, 8, im->h - 9);
        if (fx_luma(im->p + ((size_t)y * im->w + x) * 3) < 200) continue;
        const int r = fx_rand_range(rng, 5, 14);
        fx_line(im, x - r, y, x + r, y, 2, c);
        fx_line(im, x, y - r, x, y + r, 2, c);
        fx_line(im, x - r / 3, y - r / 3, x + r / 3, y + r / 3, 1, c);
        fx_line(im, x - r / 3, y + r / 3, x + r / 3, y - r / 3, 1, c);
        i++;
    }
}

// Линии скорости от краёв к центру, как в кульминации аниме.
static void speed_lines(fx_job_t *j, const uint8_t c[3], uint32_t *rng) {
    fx_rgb_t *im = &j->img;
    const float cx = (float)im->w / 2, cy = (float)im->h / 2;
    const float R = sqrtf(cx * cx + cy * cy);
    const int n = fx_rand_range(rng, 50, 90);
    for (int i = 0; i < n; i++) {
        const float a = (float)(fx_rand(rng) % 6283) / 1000.0f;
        const float r0 = R * (0.55f + (float)(fx_rand(rng) % 30) / 100.0f);
        const int t = fx_rand_range(rng, 1, 3);
        fx_line(im, (int)(cx + cosf(a) * R), (int)(cy + sinf(a) * R), (int)(cx + cosf(a) * r0),
                (int)(cy + sinf(a) * r0), t, c);
    }
}

void fx_neon_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h, n = w * h;
    const int w2 = w / 2, h2 = h / 2, n2 = w2 * h2;
    uint8_t *S = j->scratch;
    uint8_t *bits = S + 2 * (size_t)n;
    uint32_t rng = j->seed;
    fx_rand(&rng);

    neon_ctx_t k = {.j = j, .P = &PALS[j->variant % (int)(sizeof(PALS) / sizeof(PALS[0]))], .A = S, .B = S + n,
                    .bits = bits};
    const neon_pal_t *P = k.P;
    memcpy(k.col[I_INK], P->ink, 3);
    memcpy(k.col[I_PAPER], P->paper, 3);
    memcpy(k.col[I_DEEP], P->deep, 3);
    memcpy(k.col[I_RIM], P->rim, 3);
    memcpy(k.col[I_MID], P->mid, 3);
    memcpy(k.col[I_SPARK], P->spark, 3);
    for (int f = 0; f < 7; f++) {
        uint8_t *hi = k.col[I_FAM0 + f * 3];
        memcpy(k.col[I_FAM0 + f * 3 + 1], P->fam[f][0], 3);
        memcpy(k.col[I_FAM0 + f * 3 + 2], P->fam[f][1], 3);
        // Блик: светлый тон, наполовину разбавленный «бумагой»
        for (int c = 0; c < 3; c++) hi[c] = (uint8_t)((P->fam[f][0][c] * 115 + P->paper[c] * 141) >> 8);
        if (P->fam[f][0][0] == TONE_LIGHT) memcpy(hi, P->paper, 3);
    }
    static const int RD[4][2] = {{3, 2}, {-3, 2}, {3, -2}, {-2, -3}};
    const int rd = (int)(fx_rand(&rng) & 3);
    k.rdx = RD[rd][0];
    k.rdy = RD[rd][1];

    // 1. Контур
    fx_xdog(&j->img, S, S + n, 1, 2, 4);
    fx_mask_clean(S, w, h, 2, 0);
    fx_bits_pack(S, n, bits);
    fx_progress(j, 20);

    // 2. Кувахара на половинном размере
    uint8_t *H = S, *M = S + (size_t)n2 * 3, *mL = S + (size_t)n2 * 6, *mL2 = S + (size_t)n2 * 7;
    fx_down2_rgb(&j->img, H);
    // Пороги тонов по самому кадру: в тёмной воде и на ярком пляже тени
    // и блики всё равно разделятся.
    {
        for (int i = 0; i < n2; i++) mL[i] = fx_luma(H + (size_t)i * 3);
        int p40, p78;
        fx_percentiles(mL, n2, 400, 780, &p40, &p78);
        k.shade_t = fx_clampi(p40 + fx_rand_range(&rng, -6, 6), 70, 160);
        k.hi_t = fx_clampi(FX_MAX(p78, k.shade_t + 30), 120, 215);
    }
    fx_kuwahara(H, w2, h2, 3, M, mL, mL2);
    fx_progress(j, 40);
    fx_kuwahara(H, w2, h2, 2, M, mL, mL2);
    fx_progress(j, 48);
    fx_kuwahara(H, w2, h2, 1, M, mL, mL2);
    fx_progress(j, 55);

    // 3. Обратно в полный размер и раскладка по палитре
    fx_up2_rgb(H, w2, h2, &j->img);
    fx_par_rows(map_rows, &k, h);
    fx_progress(j, 70);
    fx_par_rows(mode_rows, &k, h);
    memcpy(k.A, k.B, (size_t)n);
    fx_par_rows(mode_rows, &k, h);
    fx_progress(j, 85);

    // 4. Сборка
    fx_par_rows(compose_rows, &k, h);
    if ((fx_rand(&rng) & 0xFF) < P->speed) speed_lines(j, P->manga ? P->ink : P->paper, &rng);
    if ((fx_rand(&rng) & 0xFF) < P->sparkle) sparkles(j, P->spark, &rng);
    fx_grain(&j->img, j->seed, P->manga ? 0 : 3, 0);
}
