// DMG: снимок как с Game Boy Camera.
//  - кадр 128x112 (сенсор камеры отдаёт 128x128 и срезает по 8 строк),
//    вписанный в экран 160x144 с рамкой и подписью;
//  - подчёркивание контуров как в камере: P + (4P - N - S - E - W) * a;
//  - дизеринг матрицей 4x4 с тремя порогами на клетку: < t0 чёрный,
//    < t1 тёмный, < t2 светлый, иначе белый. Средняя полоса широкая, отсюда
//    фирменный «светло-серый» вид;
//  - пиксель увеличен в 5 раз: файл 800x720, PNG 2 бита на пиксель.
#include "fx_internal.h"

#define SW 128
#define SH 112
#define GW 160
#define GH 144
#define ZOOM 5

enum { V_DMG, V_POCKET, V_SGB, V_PRINT };

// От тёмного к светлому (gb-palettes, Lospec: Super Game Boy 1-A)
static const uint8_t PALS[4][4][3] = {
    {{15, 56, 15}, {48, 98, 48}, {119, 161, 18}, {155, 188, 15}},      // DMG
    {{31, 31, 31}, {77, 83, 60}, {139, 149, 109}, {196, 207, 161}},    // POCKET
    {{48, 24, 80}, {175, 40, 80}, {223, 144, 79}, {255, 232, 207}},    // SGB 1-A
    {{40, 40, 46}, {108, 108, 112}, {176, 175, 170}, {236, 234, 226}}, // PRINT, термобумага
};

// Порядок порогов в клетке 4x4 (транспонированный Байер, как в ПЗУ камеры)
static const uint8_t ORDER[4][4] = {{0, 12, 3, 15}, {8, 4, 11, 7}, {2, 14, 1, 13}, {10, 6, 9, 5}};

static void put(uint8_t *g, int x, int y, uint8_t v) {
    if ((unsigned)x < GW && (unsigned)y < GH) g[y * GW + x] = v;
}

static void text(uint8_t *g, int x, int y, const char *s, uint8_t v) {
    for (; *s; s++, x += 6) {
        const uint8_t *gl = fx_font5x7[(uint8_t)*s & 0x7F];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (gl[r] & (0x10 >> c)) put(g, x + c, y + r, v);
    }
}

// Рамка вокруг кадра: один из трёх узоров, подпись и номер снимка.
static void frame(uint8_t *g, fx_job_t *j, uint32_t *rng, int paper) {
    const int bg = paper ? 3 : 0, fg = paper ? 0 : 2, hi = paper ? 1 : 3;
    const int pat = (int)(fx_rand(rng) % 3);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            int v = bg;
            if (pat == 0 && ((x & 3) == 1 && (y & 3) == 1)) v = fg;                  // точки
            if (pat == 1 && ((x + y) % 6 == 0)) v = fg;                               // косые полосы
            if (pat == 2 && ((x % 8 == 0 && y % 2 == 0) || (y % 8 == 0 && x % 2 == 0))) v = fg;  // схема
            g[y * GW + x] = (uint8_t)v;
        }
    // окно под снимок с двойной окантовкой
    for (int x = 13; x < 147; x++) {
        put(g, x, 13, (uint8_t)hi);
        put(g, x, 130, (uint8_t)hi);
    }
    for (int y = 13; y < 131; y++) {
        put(g, 13, y, (uint8_t)hi);
        put(g, 146, y, (uint8_t)hi);
    }
    for (int x = 4; x < 60; x++)
        for (int y = 133; y < 142; y++) put(g, x, y, (uint8_t)bg);
    text(g, 5, 134, "DITHR", (uint8_t)hi);
    char b[8];
    fx_shot_label(b, j->shot);
    for (int x = 118; x < 156; x++)
        for (int y = 133; y < 142; y++) put(g, x, y, (uint8_t)bg);
    text(g, 120, 134, b, (uint8_t)hi);
    for (int x = 60; x < 100; x++)
        for (int y = 2; y < 11; y++) put(g, x, y, (uint8_t)bg);
    text(g, 62, 3, "\x06 REC", (uint8_t)hi);
}

void fx_dmg_render(fx_job_t *j) {
    const int w = j->img.w, h = j->img.h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    const int v = j->variant % 4;
    uint8_t *small = malloc(SW * SH), *blur = malloc(SW * SH), *g = malloc(GW * GH);
    if (!small || !blur || !g) goto out;

    // 1. Вырезаем 8:7 по центру и усредняем блоками до 128x112
    const int cw = FX_MIN(w, h * 8 / 7), ch = cw * 7 / 8;
    const int ox = (w - cw) / 2, oy = (h - ch) / 2;
    for (int y = 0; y < SH; y++) {
        const int y0 = oy + y * ch / SH, y1 = FX_MAX(oy + (y + 1) * ch / SH, y0 + 1);
        for (int x = 0; x < SW; x++) {
            const int x0 = ox + x * cw / SW, x1 = FX_MAX(ox + (x + 1) * cw / SW, x0 + 1);
            int s = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t *p = j->img.p + ((size_t)yy * w + x0) * 3;
                for (int xx = x0; xx < x1; xx++, p += 3) s += fx_luma(p);
            }
            small[y * SW + x] = (uint8_t)(s / ((y1 - y0) * (x1 - x0)));
        }
    }
    fx_progress(j, 30);

    // 2. Автоконтраст и подчёркивание контуров (режим 2D камеры)
    int lo, hi;
    fx_percentiles(small, SW * SH, 15, 985, &lo, &hi);
    uint8_t lut[256];
    fx_levels_lut(lut, lo, hi);
    for (int i = 0; i < SW * SH; i++) small[i] = lut[small[i]];
    memcpy(blur, small, SW * SH);
    static const int ALPHA_Q8[4] = {192, 256, 320, 512};  // 0.75, 1, 1.25, 2
    const int a = ALPHA_Q8[fx_rand(&rng) & 3];
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            const int P = blur[y * SW + x];
            const int N = blur[FX_MAX(y - 1, 0) * SW + x], S = blur[FX_MIN(y + 1, SH - 1) * SW + x];
            const int W = blur[y * SW + FX_MAX(x - 1, 0)], E = blur[y * SW + FX_MIN(x + 1, SW - 1)];
            small[y * SW + x] = fx_u8(P + ((4 * P - N - S - E - W) * a >> 8));
        }

    // 3. Экран 160x144: рамка, затем кадр с дизерингом в окне (16,16)
    frame(g, j, &rng, v == V_PRINT);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            const int o = ORDER[y & 3][x & 3];
            const int t0 = o * 37 / 15, t1 = 41 + o * 139 / 15, t2 = 191 + o * 59 / 15;
            const int val = small[y * SW + x];
            g[(y + 16) * GW + x + 16] = (uint8_t)(val < t0 ? 0 : (val < t1 ? 1 : (val < t2 ? 2 : 3)));
        }

    // Принтер: выгоревшие столбцы печатающей головки и пропуски точек
    if (v == V_PRINT) {
        for (int k = 0; k < 3; k++) {
            const int col = fx_rand_range(&rng, 16, 143);
            for (int y = 16; y < 128; y++)
                if ((fx_rand(&rng) & 3) && g[y * GW + col] < 3) g[y * GW + col]++;
        }
        for (int i = 0; i < 200; i++) {
            const int x = fx_rand_range(&rng, 16, 143), y = fx_rand_range(&rng, 16, 127);
            if (g[y * GW + x] < 3) g[y * GW + x]++;
        }
    }

    // 4. Увеличение x5 в idx (в scratch)
    const int ow = GW * ZOOM, oh = GH * ZOOM;
    uint8_t *idx = j->scratch;
    for (int y = 0; y < oh; y++) {
        const uint8_t *src = g + (y / ZOOM) * GW;
        uint8_t *o = idx + (size_t)y * ow;
        for (int x = 0; x < ow; x++) o[x] = src[x / ZOOM];
    }
    j->idx = idx;
    j->out_w = ow;
    j->out_h = oh;
    memcpy(j->pal, PALS[v], sizeof(PALS[v]));
    j->pal_n = 4;
    j->bpp = 2;
out:
    free(small);
    free(blur);
    free(g);
}
