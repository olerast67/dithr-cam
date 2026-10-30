// Живое превью на однобитном OLED 128x96. Каждый стиль показывает свой
// характер упрощённо: экран чёрно-белый, цвет будет только в файле.
// Для видео берётся упорядоченный дизеринг (Байер): у него узор стоит на
// месте от кадра к кадру. Рассеивание ошибки в видео «кипит».
#include <math.h>

#include "fx_internal.h"

void fx_preview_init(fx_prev_t *st) {
    memset(st, 0, sizeof(*st));
    st->rng = 0x1234567u;
    st->lo_q8 = 16 << 8;
    st->hi_q8 = 235 << 8;
    st->band_t = UINT32_MAX;
}

static inline uint8_t bayer4_bit(int v, int x, int y) { return v > fx_bayer4[y & 3][x & 3]; }

static void dither_bayer4(const uint8_t *g, uint8_t *m) {
    for (int y = 0; y < FX_PH; y++)
        for (int x = 0; x < FX_PW; x++) m[y * FX_PW + x] = bayer4_bit(g[y * FX_PW + x], x, y);
}

static void dither_bayer8(const uint8_t *g, uint8_t *m) {
    for (int y = 0; y < FX_PH; y++)
        for (int x = 0; x < FX_PW; x++) m[y * FX_PW + x] = g[y * FX_PW + x] > fx_bayer8[y & 7][x & 7];
}

// Автоконтраст с плавным следованием, чтобы картинка не дёргалась.
static void autolevels(fx_prev_t *st, uint8_t *g) {
    int lo, hi;
    fx_percentiles(g, FX_PN, 10, 990, &lo, &hi);
    st->lo_q8 += ((lo << 8) - st->lo_q8) / 6;
    st->hi_q8 += ((hi << 8) - st->hi_q8) / 6;
    int l = st->lo_q8 >> 8, h = st->hi_q8 >> 8;
    if (h - l < 48) h = l + 48;
    uint8_t lut[256];
    fx_levels_lut(lut, l, h);
    for (int i = 0; i < FX_PN; i++) g[i] = lut[g[i]];
}

// Полосы для протяжек и сбоев обновляются раз в несколько кадров.
static void bands_update(fx_prev_t *st, int every, int max_h) {
    if (st->band_t != UINT32_MAX && st->t - st->band_t < (uint32_t)every) return;
    st->band_t = st->t;
    for (int i = 0; i < 4; i++) {
        st->band[i][1] = (int16_t)fx_rand_range(&st->rng, 2, max_h);
        st->band[i][0] = (int16_t)fx_rand_range(&st->rng, 0, FX_PH - st->band[i][1]);
        st->band[i][2] = (int16_t)fx_rand_range(&st->rng, -FX_PW / 3, FX_PW / 3);
    }
}

// ---------------------------------------------------------------- стили
static void prev_neon(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    uint8_t *blur = st->tmp, *mag = st->tmp2;
    memcpy(blur, g, FX_PN);
    fx_blur_plane(blur, FX_PW, FX_PH, 1, 1);
    fx_sobel(blur, mag, FX_PW, FX_PH);
    for (int y = 0; y < FX_PH; y++)
        for (int x = 0; x < FX_PW; x++) {
            const int i = y * FX_PW + x;
            const int v = blur[i];
            uint8_t bit;
            if (v < 72) bit = 0;
            else if (v < 165) bit = (x ^ y) & 1;  // ровная штриховка 50% = «тон»
            else bit = 1;
            if (mag[i] > 34) bit = 0;  // чернильный контур
            m[i] = bit;
        }
}

static void prev_y2k(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    for (int i = 0; i < FX_PN; i++) {
        const int t = 255 - g[i];
        g[i] = (uint8_t)(255 - t * t / 255);  // пересвет
    }
    bands_update(st, 20, 14);
    for (int b = 0; b < 2; b++) {
        const int y0 = st->band[b][0], bh = st->band[b][1];
        const int x0 = FX_PW / 2 + st->band[b][2];
        const int dir = (b & 1) ? -1 : 1;
        for (int y = y0; y < y0 + bh; y++) {
            uint8_t *row = g + y * FX_PW;
            const int xs = fx_clampi(x0 + ((int)(fx_rand(&st->rng) % 5) - 2), 0, FX_PW - 1);
            for (int x = xs; x >= 0 && x < FX_PW; x += dir) row[x] = row[xs];
        }
    }
    dither_bayer4(g, m);
    // Вращающееся пунктирное кольцо и уголки
    const float base = (float)st->t * 0.06f;
    for (int a = 0; a < 220; a += 2) {
        if ((a / 10) % 3 == 2) continue;
        const float r = base + (float)a * 0.0174533f;
        fx_m_set(m, 100 + (int)lroundf(24.0f * cosf(r)), 26 + (int)lroundf(24.0f * sinf(r)), 1);
    }
    for (int i = 0; i < 9; i++) {
        fx_m_set(m, 3 + i, 3, 1);
        fx_m_set(m, 3, 3 + i, 1);
        fx_m_set(m, FX_PW - 4 - i, FX_PH - 4, 1);
        fx_m_set(m, FX_PW - 4, FX_PH - 4 - i, 1);
    }
}

static void prev_melt(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    // Порог «дышит», чтобы было видно, что картинка течёт.
    const int lo = 70 + (int)(30.0f * sinf((float)st->t * 0.08f));
    const int hi = 235;
    for (int x = 0; x < FX_PW; x++) {
        int y = 0;
        while (y < FX_PH) {
            if (g[y * FX_PW + x] < lo || g[y * FX_PW + x] > hi) {
                y++;
                continue;
            }
            int n = 0;
            while (y + n < FX_PH) {
                const int v = g[(y + n) * FX_PW + x];
                if (v < lo || v > hi) break;
                n++;
            }
            // вставками: полоса не длиннее 96
            for (int i = 1; i < n; i++) {
                const uint8_t v = g[(y + i) * FX_PW + x];
                int k = i - 1;
                while (k >= 0 && g[(y + k) * FX_PW + x] > v) {
                    g[(y + k + 1) * FX_PW + x] = g[(y + k) * FX_PW + x];
                    k--;
                }
                g[(y + k + 1) * FX_PW + x] = v;
            }
            y += n;
        }
    }
    dither_bayer4(g, m);
}

static void prev_ghost(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    const uint8_t *old = st->hist[(st->t + 1) & 3];  // кадр трёхкадровой давности
    dither_bayer4(g, m);
    if (st->t >= 4)
        for (int i = 0; i < FX_PN; i++)
            if (FX_ABS((int)g[i] - (int)old[i]) > 40) m[i] ^= 1;
    memcpy(st->hist[st->t & 3], g, FX_PN);
}

static void prev_mosh(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    bands_update(st, 7, 20);
    uint8_t *row = st->tmp;
    for (int b = 0; b < 3; b++) {
        const int y0 = st->band[b][0], bh = st->band[b][1], dx = st->band[b][2];
        for (int y = y0; y < y0 + bh; y++) {
            memcpy(row, g + y * FX_PW, FX_PW);
            for (int x = 0; x < FX_PW; x++) g[y * FX_PW + x] = row[((x - dx) % FX_PW + FX_PW) % FX_PW];
        }
    }
    // Тянущиеся блоки 8x8 из четвёртой полосы
    const int bx = (st->band[3][2] + FX_PW / 2) & ~7;
    const int by = st->band[3][0] & ~7;
    for (int k = 1; k < 5 && by + k * 8 + 8 <= FX_PH; k++)
        for (int y = 0; y < 8; y++)
            memcpy(g + (by + k * 8 + y) * FX_PW + bx, g + (by + y) * FX_PW + bx, FX_MIN(24, FX_PW - bx));
    dither_bayer4(g, m);
}

// Каждый пиксель Game Boy = квадрат 2x2 OLED с узором: 0, 1, 2 или 4 точки.
static void prev_dmg(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    (void)st;
    static const uint8_t PAT[4][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 1, 1, 1}};
    for (int y = 0; y < FX_PH / 2; y++)
        for (int x = 0; x < FX_PW / 2; x++) {
            const int i = 2 * y * FX_PW + 2 * x;
            const int v = (g[i] + g[i + 1] + g[i + FX_PW] + g[i + FX_PW + 1]) >> 2;
            const int q = v * 3;
            int lvl = q / 255;
            if ((q % 255) > fx_bayer4[y & 3][x & 3]) lvl++;
            if (lvl > 3) lvl = 3;
            const uint8_t *p = PAT[lvl];
            m[i] = p[0];
            m[i + 1] = p[1];
            m[i + FX_PW] = p[2];
            m[i + FX_PW + 1] = p[3];
        }
}

static void prev_ink(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    int16_t *err = (int16_t *)st->tmp;  // 3 строки по FX_PW+4, помещаются в tmp
    const int ew = FX_PW + 4;
    memset(err, 0, sizeof(int16_t) * (size_t)ew * 3);
    for (int y = 0; y < FX_PH; y++) {
        int16_t *e0 = err + (y % 3) * ew + 2, *e1 = err + ((y + 1) % 3) * ew + 2, *e2 = err + ((y + 2) % 3) * ew + 2;
        for (int x = 0; x < FX_PW; x++) {
            const int v = g[y * FX_PW + x] + e0[x];
            const int out = v > 127 ? 255 : 0;
            const int e = (v - out) >> 3;
            m[y * FX_PW + x] = out ? 1 : 0;
            e0[x + 1] += (int16_t)e;
            e0[x + 2] += (int16_t)e;
            e1[x - 1] += (int16_t)e;
            e1[x] += (int16_t)e;
            e1[x + 1] += (int16_t)e;
            e2[x] += (int16_t)e;
        }
        memset(e0 - 2, 0, sizeof(int16_t) * (size_t)ew);
    }
}

void fx_preview(fx_style_t s, fx_prev_t *st, uint8_t *g, uint8_t *m) {
    autolevels(st, g);
    switch (s) {
        case FX_NEON: prev_neon(st, g, m); break;
        case FX_Y2K: prev_y2k(st, g, m); break;
        case FX_MELT: prev_melt(st, g, m); break;
        case FX_GHOST: prev_ghost(st, g, m); break;
        case FX_MOSH: prev_mosh(st, g, m); break;
        case FX_DMG: prev_dmg(st, g, m); break;
        case FX_INK: prev_ink(st, g, m); break;
        case FX_TIDE: fx_prev_tide(st, g, m); break;
        case FX_PULSE: fx_prev_pulse(st, g, m); break;
        case FX_SHARD: fx_prev_shard(st, g, m); break;
        case FX_HOLO: fx_prev_holo(st, g, m); break;
        case FX_LOOP: fx_prev_loop(st, g, m); break;
        case FX_TOPO: fx_prev_topo(st, g, m); break;
        default: dither_bayer8(g, m); break;
    }
    st->t++;
}
