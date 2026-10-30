// Живые превью новых стилей на однобитном экране 128x96.
// Многие из них двигаются сами: спираль LOOP вращается, объём HOLO
// покачивается, TIDE показывает щелевую развёртку из последних кадров.
#include <math.h>

#include "fx_internal.h"

#define W FX_PW
#define H FX_PH

static inline void px(uint8_t *m, int x, int y, uint8_t v) {
    if ((unsigned)x < W && (unsigned)y < H) m[y * W + x] = v;
}

// TIDE: столбец x показывает кадр давностью (K-1) * (1 - x/W): слева прошлое.
void fx_prev_tide(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    const int K = FX_TIDE_FRAMES;
    memcpy(st->tide[st->t % K], g, FX_PN);
    for (int x = 0; x < W; x++) {
        const int age = (K - 1) - x * (K - 1) / (W - 1);
        const uint8_t *f = st->t >= (uint32_t)age ? st->tide[(st->t - age) % K] : g;
        for (int y = 0; y < H; y++) m[y * W + x] = f[y * W + x] > fx_bayer4[y & 3][x & 3];
    }
}

// PULSE: 24 линии, приподнятые яркостью, ближние закрывают дальние.
void fx_prev_pulse(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    (void)st;
    memset(m, 0, FX_PN);
    const int S = 4, A = 14;
    for (int y0 = A; y0 < H - 1; y0 += S) {
        int prev = -1;
        for (int x = 0; x < W; x++) {
            const int env = x < 10 ? x * 26 : (x > W - 11 ? (W - 1 - x) * 26 : 256);
            const int v = g[y0 * W + x];
            const int y = y0 - ((v * v / 255) * A / 255) * env / 256;
            for (int yy = y + 1; yy <= y0 && yy < H; yy++) m[yy * W + x] = 0;  // заслонка
            if (prev < 0) prev = y;
            for (int yy = FX_MIN(prev, y); yy <= FX_MAX(prev, y); yy++) px(m, x, yy, 1);
            prev = y;
        }
    }
}

// TOPO: горизонтали размытой яркости, индексные толще.
void fx_prev_topo(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    uint8_t *b = st->tmp;
    memcpy(b, g, FX_PN);
    fx_blur_plane(b, W, H, 1, 2);
    fx_blur_plane(b, W, H, 1, 2);
    const int step = 20;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int l = b[y * W + x] / step;
            const int r = x < W - 1 ? b[y * W + x + 1] / step : l, d = y < H - 1 ? b[(y + 1) * W + x] / step : l;
            m[y * W + x] = (uint8_t)(l != r || l != d);
        }
}

// SHARD: 70 центров, ячейки залиты своим средним тоном через Байера.
void fx_prev_shard(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    enum { NC = 70 };
    static int16_t cx[NC], cy[NC];
    static int inited;
    if (!inited) {
        uint32_t r = 0xC0FFEE;
        for (int i = 0; i < NC; i++) {
            cx[i] = (int16_t)(fx_rand(&r) % W);
            cy[i] = (int16_t)(fx_rand(&r) % H);
        }
        inited = 1;
    }
    uint8_t *id = st->tmp;
    int sum[NC] = {0}, cnt[NC] = {0};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int best = 0, bd = 1 << 30;
            for (int i = 0; i < NC; i++) {
                const int dx = cx[i] - x, dy = cy[i] - y, d = dx * dx + dy * dy;
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
            id[y * W + x] = (uint8_t)best;
            sum[best] += g[y * W + x];
            cnt[best]++;
        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int c = id[y * W + x];
            const int seam = (x < W - 1 && id[y * W + x + 1] != c) || (y < H - 1 && id[(y + 1) * W + x] != c);
            const int v = sum[c] / FX_MAX(cnt[c], 1);
            m[y * W + x] = seam ? 1 : (uint8_t)(v > fx_bayer4[y & 3][x & 3]);
        }
}

// HOLO: облако точек, медленно покачивается вокруг вертикальной оси.
void fx_prev_holo(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    memset(m, 0, FX_PN);
    const float yaw = 0.5f * sinf((float)st->t * 0.04f), tilt = 0.75f;
    const float cb = cosf(yaw), sb = sinf(yaw), ca = cosf(tilt), sa = sinf(tilt), f = 160.0f;
    for (int y = 0; y < H; y += 3)
        for (int x = 0; x < W; x += 2) {
            const float X = (float)(x - W / 2), Y = (float)(y - H / 2), Z = -(float)g[y * W + x] * 0.12f;
            const float X1 = X * cb + Z * sb, Z1 = -X * sb + Z * cb;
            const float Y2 = Y * ca - Z1 * sa, Z2 = Y * sa + Z1 * ca;
            const float d = f + Z2;
            if (d < 20.0f) continue;
            px(m, W / 2 + (int)(X1 * f / d), H / 2 + 6 + (int)(Y2 * f / d), 1);
        }
}

// LOOP: спираль Дросте, медленно вращается.
void fx_prev_loop(fx_prev_t *st, uint8_t *g, uint8_t *m) {
    const float s = 0.45f, bi = -logf(s) / 6.2832f, rot = (float)st->t * 0.035f;
    const float W2 = W / 2.0f, H2 = H / 2.0f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const float zx = (float)x - W2 + 0.5f, zy = (float)y - H2 + 0.5f;
            const float lr = 0.5f * logf(zx * zx + zy * zy), th = atan2f(zy, zx) + rot;
            const float wr = lr - th * bi, wi = lr * bi + th;
            const float mg = expf(wr);
            float sx = mg * cosf(wi), sy = mg * sinf(wi);
            for (int it = 0; it < 16 && (fabsf(sx) > W2 || fabsf(sy) > H2); it++) {
                sx *= s;
                sy *= s;
            }
            for (int it = 0; it < 16 && fabsf(sx) < W2 * s && fabsf(sy) < H2 * s; it++) {
                sx /= s;
                sy /= s;
            }
            if (fabsf(sx) > W2 * 0.96f || fabsf(sy) > H2 * 0.95f) {
                m[y * W + x] = 1;  // рамка уровня
                continue;
            }
            const int ix = fx_clampi((int)(sx + W2), 0, W - 1), iy = fx_clampi((int)(sy + H2), 0, H - 1);
            m[y * W + x] = g[iy * W + ix] > fx_bayer4[y & 3][x & 3];
        }
}
