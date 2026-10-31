// Y2K: эстетика нулевых. Кадр закручивается воронкой вокруг самого яркого
// объекта, светлое выжигается белым, тени уходят в индиго, по краям
// разъезжаются цвета, полосы пикселей тянутся вбок. Поверх векторный HUD,
// который «видит» кадр: прицел на объекте, волна по горизонту, окно ошибки
// в духе Windows 98, орбиты, крупное слово. Компоновка своя у каждого снимка.
#include <math.h>
#include <stdio.h>

#include "fx_internal.h"

typedef struct {
    fx_stop_t grad[5];
    uint8_t hud[4][3];  // акценты интерфейса
} y2k_pal_t;

static const y2k_pal_t PALS[] = {
    // ICE
    {{{0, {20, 10, 82}}, {60, {110, 62, 232}}, {120, {80, 168, 255}}, {185, {190, 238, 255}}, {255, {255, 255, 255}}},
     {{255, 40, 60}, {255, 0, 200}, {30, 70, 255}, {40, 230, 110}}},
    // CHROME
    {{{0, {10, 12, 24}}, {70, {64, 76, 110}}, {140, {165, 178, 204}}, {200, {226, 234, 246}}, {255, {255, 255, 255}}},
     {{0, 96, 255}, {255, 120, 0}, {255, 255, 255}, {0, 220, 255}}},
    // SUNSET
    {{{0, {40, 0, 64}}, {70, {178, 30, 122}}, {135, {255, 110, 84}}, {195, {255, 204, 128}}, {255, {255, 250, 232}}},
     {{0, 230, 255}, {255, 240, 60}, {255, 255, 255}, {120, 40, 255}}},
    // TOXIC
    {{{0, {0, 12, 20}}, {70, {0, 92, 64}}, {140, {120, 255, 84}}, {200, {226, 255, 184}}, {255, {255, 255, 255}}},
     {{255, 0, 170}, {255, 255, 255}, {180, 255, 0}, {0, 200, 255}}},
};

static const uint8_t WHITE[3] = {255, 255, 255};
static const uint8_t BLACK[3] = {0, 0, 0};

typedef struct {
    fx_job_t *j;
    const uint8_t *L;
    uint8_t lift[256];
    uint8_t grad[256][3];
    int mix;  // доля карты градиента, из 256
} y2k_ctx_t;

static void tone_rows(void *c, int y0, int y1) {
    y2k_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = k->j->img.p + (size_t)y * w * 3;
        const uint8_t *L = k->L + (size_t)y * w;
        for (int x = 0; x < w; x++, d += 3) {
            const int l = L[x], lm = k->lift[l], dl = lm - l;
            const uint8_t *g = k->grad[lm];
            for (int ch = 0; ch < 3; ch++) {
                // исходный цвет: поднят по яркости и насыщенность x1.4
                const int o = fx_u8(l + dl + ((d[ch] - l) * 7 >> 2) - ((d[ch] - l) * 7 >> 2) / 4);
                d[ch] = (uint8_t)((g[ch] * k->mix + o * (256 - k->mix)) >> 8);
            }
        }
    }
}

// Мягкое белое пятно-«выжигание», как пересвеченный дым в видео.
static void burn_blob(fx_rgb_t *im, int cx, int cy, int r) {
    const int x0 = FX_MAX(cx - r, 0), x1 = FX_MIN(cx + r, im->w);
    const int y0 = FX_MAX(cy - r, 0), y1 = FX_MIN(cy + r, im->h);
    const int rr = r * r;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = im->p + ((size_t)y * im->w + x0) * 3;
        for (int x = x0; x < x1; x++, d += 3) {
            const int dd = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (dd >= rr) continue;
            const int f = 256 - dd * 256 / rr;
            const int a = f * f >> 8;
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)(d[c] + (((255 - d[c]) * a) >> 8));
        }
    }
}

static void streaks(fx_rgb_t *im, uint32_t *rng) {
    const int w = im->w, h = im->h;
    const int bands = fx_rand_range(rng, 3, 6);
    for (int b = 0; b < bands; b++) {
        const int bh = fx_rand_range(rng, h / 40 + 1, h / 6);
        const int by = fx_rand_range(rng, 0, h - bh);
        const int x0 = fx_rand_range(rng, w * 10 / 100, w * 90 / 100);
        const int dir = (fx_rand(rng) & 1) ? 1 : -1;
        const int keep = fx_rand_range(rng, 40, 130);  // сколько исходника проступает к концу
        int jit = 0;
        for (int y = by; y < by + bh; y++) {
            jit = fx_clampi(jit + fx_rand_range(rng, -2, 2), -w / 50, w / 50);
            const int xs = fx_clampi(x0 + jit, 0, w - 1);
            uint8_t *row = im->p + (size_t)y * w * 3;
            const uint8_t src[3] = {row[xs * 3], row[xs * 3 + 1], row[xs * 3 + 2]};
            const int len = dir > 0 ? w - 1 - xs : xs;
            for (int i = 1; i <= len; i++) {
                uint8_t *p = row + (xs + dir * i) * 3;
                const int f = i * keep / FX_MAX(len, 1);
                for (int ch = 0; ch < 3; ch++) p[ch] = (uint8_t)((src[ch] * (256 - f) + p[ch] * f) >> 8);
            }
        }
    }
}

// ---------------------------------------------------------------- HUD
static int S(const fx_rgb_t *im, int k) { return FX_MAX(1, im->w * k / 800); }  // масштаб к 800 px

static void hud_target(fx_rgb_t *im, int cx, int cy, int r, const y2k_pal_t *P, uint32_t *rng) {
    const int t = S(im, 5);
    float a = (float)(fx_rand(rng) % 628) / 100.0f;
    const float end = a + 6.2832f;
    int si = (int)(fx_rand(rng) & 3);
    while (a < end) {
        const float span = 0.25f + (float)(fx_rand(rng) % 110) / 100.0f;
        const float b = FX_MIN(a + span, end);
        fx_arc(im, cx, cy, r, t, a, b - 0.06f, si == 3 ? WHITE : P->hud[si]);
        si = (si + 1) & 3;
        a = b;
    }
    fx_arc(im, cx, cy, r - 3 * t, 2, 0.3f, 4.0f, WHITE);
    const int g = r / 3;
    fx_line(im, cx - r - g, cy, cx - r + g, cy, 2, WHITE);
    fx_line(im, cx + r - g, cy, cx + r + g, cy, 2, WHITE);
    fx_line(im, cx, cy - r - g, cx, cy - r + g, 2, WHITE);
    fx_line(im, cx, cy + r - g, cx, cy + r + g, 2, WHITE);
    // подпись с выноской
    const int lx = cx + (cx < im->w / 2 ? r + S(im, 40) : -r - S(im, 220));
    const int ly = cy - r / 2;
    fx_line(im, cx + (lx > cx ? r : -r) * 7 / 10, cy - r * 7 / 10, lx + (lx > cx ? 0 : S(im, 180)), ly, 2, WHITE);
    fx_blend_rect(im, lx, ly, S(im, 180), S(im, 44), BLACK, 150);
    char b[24];
    snprintf(b, sizeof(b), "TARGET_%02X", (unsigned)(fx_rand(rng) & 0xFF));
    fx_text(im, lx + S(im, 6), ly + S(im, 6), S(im, 2), b, WHITE);
    snprintf(b, sizeof(b), "LOCK %u.%u%%", 90 + (unsigned)(fx_rand(rng) % 10), (unsigned)(fx_rand(rng) % 10));
    fx_text(im, lx + S(im, 6), ly + S(im, 26), S(im, 2), b, P->hud[0]);
}

static void hud_signal(fx_rgb_t *im, int hy, const y2k_pal_t *P, uint32_t *rng) {
    const int w = im->w, t = S(im, 3);
    for (int k = 0; k < 2; k++) {
        const float amp = (float)(im->h * fx_rand_range(rng, 3, 9) / 100);
        const float f1 = 6.2832f / (float)fx_rand_range(rng, w / 4, w / 2);
        const float f2 = 6.2832f / (float)fx_rand_range(rng, w / 12, w / 6);
        const float ph = (float)(fx_rand(rng) % 628) / 100.0f;
        int px = 0, py = hy;
        for (int x = 4; x <= w; x += 4) {
            const float env = sinf(3.14159f * (float)x / (float)w);
            const int y = hy + (int)(amp * env * (sinf(ph + f1 * (float)x) + 0.35f * sinf(f2 * (float)x + ph * 2)));
            fx_line(im, px, py, x, y, t, P->hud[k + 1]);
            px = x;
            py = y;
        }
    }
    static const char *const WORDS[] = {"SPACE", "SIGNAL", "DREAM", "VOID", "AWAKE", "ONLINE", "ERROR", "LOVE.EXE"};
    const char *word = WORDS[fx_rand(rng) % 8];
    const int sc = S(im, fx_rand_range(rng, 7, 11));
    const int tw = fx_text_width(word, sc);
    const int tx = fx_rand_range(rng, S(im, 30), FX_MAX(S(im, 31), im->w - tw - S(im, 30)));
    const int ty = hy > im->h / 2 ? hy - sc * 7 - S(im, 50) : hy + S(im, 50);
    fx_text_outlined(im, tx, ty, sc, word, WHITE, P->hud[2][0] > 200 && P->hud[2][1] > 200 ? P->hud[0] : P->hud[2]);
    // стрелка
    const int ax = tx + tw + S(im, 20), ay = ty + sc * 2, al = S(im, 46);
    fx_line(im, ax, ay, ax + al, ay + al, S(im, 6), WHITE);
    fx_line(im, ax + al, ay + al, ax + al - al / 2, ay + al, S(im, 6), WHITE);
    fx_line(im, ax + al, ay + al, ax + al, ay + al - al / 2, S(im, 6), WHITE);
}

// Окно ошибки в стиле Windows 98: серое тело, синий заголовок с градиентом.
static void hud_window(fx_rgb_t *im, int avoid_x, int avoid_y, uint32_t *rng) {
    const int ww = S(im, 300), wh = S(im, 130);
    int x = avoid_x < im->w / 2 ? im->w - ww - S(im, 40) : S(im, 40);
    int y = avoid_y < im->h / 2 ? im->h - wh - S(im, 130) : S(im, 70);  // выше номера кадра
    x += fx_rand_range(rng, -S(im, 20), S(im, 20));
    static const uint8_t GRAY[3] = {192, 192, 192}, DARK[3] = {128, 128, 128};
    fx_fill_rect(im, x + S(im, 6), y + S(im, 6), ww, wh, BLACK);  // тень
    fx_fill_rect(im, x, y, ww, wh, GRAY);
    fx_fill_rect(im, x, y, ww, 2, WHITE);
    fx_fill_rect(im, x, y, 2, wh, WHITE);
    fx_fill_rect(im, x, y + wh - 2, ww, 2, DARK);
    fx_fill_rect(im, x + ww - 2, y, 2, wh, DARK);
    const int th = S(im, 24);
    for (int i = 0; i < ww - 8; i++) {
        const uint8_t c[3] = {0, (uint8_t)(i * 132 / ww), (uint8_t)(128 + i * 80 / ww)};
        fx_fill_rect(im, x + 4 + i, y + 4, 1, th, c);
    }
    static const char *const T[] = {"DREAM.EXE", "SIGNAL.EXE", "MEMORY.SYS", "SELF.DLL"};
    static const char *const M1[] = {"NOT RESPONDING", "SIGNAL LOST", "REALITY ERROR", "OUT OF DREAMS"};
    const int k = (int)(fx_rand(rng) & 3);
    fx_text(im, x + S(im, 10), y + S(im, 10), S(im, 2), T[k], WHITE);
    fx_fill_rect(im, x + ww - S(im, 26), y + S(im, 8), S(im, 18), S(im, 16), GRAY);
    fx_text(im, x + ww - S(im, 22), y + S(im, 10), S(im, 2), "X", BLACK);
    fx_text(im, x + S(im, 20), y + S(im, 50), S(im, 2), M1[(k + fx_rand(rng)) & 3], BLACK);
    const int bx = x + ww / 2 - S(im, 40), by = y + wh - S(im, 42);
    fx_fill_rect(im, bx, by, S(im, 80), S(im, 28), GRAY);
    fx_frame_rect(im, bx, by, S(im, 80), S(im, 28), 2, BLACK);
    fx_text(im, bx + S(im, 28), by + S(im, 7), S(im, 2), "OK", BLACK);
    // курсор-стрелка
    const int mx = bx + S(im, 60), my = by + S(im, 18), ml = S(im, 26);
    for (int i = 0; i < ml; i++) fx_fill_rect(im, mx, my + i, i * 2 / 3 + 1, 1, WHITE);
    fx_line(im, mx, my, mx, my + ml, 1, BLACK);
    fx_line(im, mx, my, mx + ml * 2 / 3, my + ml, 1, BLACK);
}

static void hud_orbit(fx_rgb_t *im, int cx, int cy, int r, const y2k_pal_t *P, uint32_t *rng) {
    for (int i = 0; i < 3; i++) {
        const int rr = r + S(im, 30) * (i + 1);
        const float a0 = (float)(fx_rand(rng) % 628) / 100.0f;
        fx_arc(im, cx, cy, rr, 2, a0, a0 + 2.0f + (float)i, i == 1 ? P->hud[3] : WHITE);
        const float ad = a0 + 2.0f + (float)i;
        const int dx = cx + (int)(cosf(ad) * (float)rr), dy = cy + (int)(sinf(ad) * (float)rr);
        fx_fill_rect(im, dx - S(im, 6), dy - S(im, 6), S(im, 12), S(im, 12), P->hud[i]);
    }
}

static void hud_frame(fx_job_t *j, const y2k_pal_t *P) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h, m = S(im, 22), cl = S(im, 50), ct = S(im, 4);
    fx_fill_rect(im, m, m, cl, ct, WHITE);
    fx_fill_rect(im, m, m, ct, cl, WHITE);
    fx_fill_rect(im, w - m - cl, h - m - ct, cl, ct, WHITE);
    fx_fill_rect(im, w - m - ct, h - m - cl, ct, cl, WHITE);
    char shot[8];
    fx_shot_label(shot, j->shot);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "DITHR//CAM \x01 Y2K.%s", fx_info(FX_Y2K)->var[j->variant]);
    fx_text(im, m + ct * 3, m + ct * 3, S(im, 2), hdr, WHITE);
    const int s2 = S(im, 6);
    fx_text_outlined(im, m + ct * 3, h - m - ct * 3 - 7 * s2, s2, shot, WHITE, P->hud[0]);
}

void fx_y2k_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h, n = w * h;
    uint8_t *A = j->scratch, *B = j->scratch + n;
    const y2k_pal_t *P = &PALS[j->variant % (int)(sizeof(PALS) / sizeof(PALS[0]))];
    uint32_t rng = j->seed;
    fx_rand(&rng);

    int hx, hy;
    fx_find_hotspot(&j->img, &hx, &hy);

    // 1. Воронка (в 3 из 4 снимков)
    const int R = fx_rand_range(&rng, h * 25 / 100, h * 45 / 100);
    if ((fx_rand(&rng) & 3) != 0) {
        const float amt = (float)fx_rand_range(&rng, 150, 380) / 100.0f * ((fx_rand(&rng) & 1) ? 1.0f : -1.0f);
        fx_swirl(&j->img, hx, hy, R, amt, j->scratch, fx_scratch_size(w, h));
    }
    fx_progress(j, 20);

    // 2. Тон: подъём, карта градиента
    y2k_ctx_t *k = malloc(sizeof(y2k_ctx_t));
    if (!k) {
        j->failed = true;
        return;
    }
    k->j = j;
    k->L = A;
    const float gam = 1.5f + (float)(fx_rand(&rng) % 60) / 100.0f;
    for (int v = 0; v < 256; v++) k->lift[v] = fx_u8((int)lroundf(255.0f * (1.0f - powf(1.0f - (float)v / 255.0f, gam))));
    fx_gradient_lut(k->grad, P->grad, 5);
    k->mix = fx_rand_range(&rng, 140, 200);
    fx_luma_plane(&j->img, A);
    const int horizon = fx_find_horizon(A, w, h);
    fx_par_rows(tone_rows, k, h);
    free(k);
    fx_progress(j, 40);

    // 3. Выжигание: свечение и белые пятна
    fx_bloom(&j->img, fx_rand_range(&rng, 150, 185), fx_rand_range(&rng, 600, 900), FX_MAX(3, w / 100), B);
    const int blobs = fx_rand_range(&rng, 1, 3);
    for (int i = 0; i < blobs; i++)
        burn_blob(&j->img, hx + fx_rand_range(&rng, -w / 6, w / 6), hy + fx_rand_range(&rng, -h / 6, h / 6),
                  fx_rand_range(&rng, h / 8, h / 3));
    fx_progress(j, 55);

    // 4. Протяжки и разъезд цвета к краям
    streaks(&j->img, &rng);
    const float ca = (float)fx_rand_range(&rng, 4, 12) / 1000.0f;
    fx_ca_channel(&j->img, 0, 1.0f + ca, B);
    fx_ca_channel(&j->img, 2, 1.0f - ca, B);
    fx_progress(j, 75);

    // 5. HUD: две случайные «сцены» из четырёх, привязанные к кадру
    const int pick = fx_rand_range(&rng, 0, 5);
    static const uint8_t COMBOS[6][2] = {{0, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {2, 3}};
    for (int i = 0; i < 2; i++) {
        switch (COMBOS[pick][i]) {
            case 0: hud_target(&j->img, hx, hy, FX_MAX(R * 6 / 10, h / 8), P, &rng); break;
            case 1: hud_signal(&j->img, horizon, P, &rng); break;
            case 2: hud_window(&j->img, hx, hy, &rng); break;
            default: hud_orbit(&j->img, hx, hy, FX_MAX(R / 2, h / 10), P, &rng); break;
        }
    }
    hud_frame(j, P);

    // 6. Строки развёртки (у CHROME всегда, у остальных иногда) и зерно
    if (j->variant == 1 || (fx_rand(&rng) & 3) == 0)
        for (int y = 0; y < h; y += 3) {
            uint8_t *row = j->img.p + (size_t)y * w * 3;
            for (int x = 0; x < w * 3; x++) row[x] = (uint8_t)(row[x] * 220 >> 8);
        }
    fx_grain(&j->img, j->seed ^ 0x77, 7, 40);
}
