// Инструменты, из которых собираются стили: масштаб 2x, фильтр Кувахары,
// линии XDoG, свечение, разъезд каналов, закрутка, зерно, градиентные карты.
#include <math.h>

#include "fx_internal.h"

// ---------------------------------------------------------------- масштаб
void fx_down2_rgb(const fx_rgb_t *src, uint8_t *dst) {
    const int w2 = src->w / 2, h2 = src->h / 2, sw = src->w;
    for (int y = 0; y < h2; y++) {
        const uint8_t *a = src->p + (size_t)(2 * y) * sw * 3;
        const uint8_t *b = a + (size_t)sw * 3;
        uint8_t *o = dst + (size_t)y * w2 * 3;
        for (int x = 0; x < w2; x++, a += 6, b += 6, o += 3)
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)((a[c] + a[c + 3] + b[c] + b[c + 3] + 2) >> 2);
    }
}

typedef struct {
    const uint8_t *s;
    int sw, sh;
    fx_rgb_t *d;
} up2_ctx_t;

static void up2_rows(void *c, int y0, int y1) {
    up2_ctx_t *k = c;
    const int dw = k->d->w;
    for (int y = y0; y < y1; y++) {
        int fy = y * 64 - 32;  // (y+0.5)/2-0.5 в формате 7 бит дроби
        if (fy < 0) fy = 0;
        int sy = fy >> 7;
        if (sy >= k->sh - 1) {
            sy = k->sh - 2;
            fy = (sy << 7) | 127;
        }
        const int wy = fy & 127;
        const uint8_t *r0 = k->s + (size_t)sy * k->sw * 3, *r1 = r0 + (size_t)k->sw * 3;
        uint8_t *o = k->d->p + (size_t)y * dw * 3;
        for (int x = 0; x < dw; x++, o += 3) {
            int fx = x * 64 - 32;
            if (fx < 0) fx = 0;
            int sx = fx >> 7;
            if (sx >= k->sw - 1) {
                sx = k->sw - 2;
                fx = (sx << 7) | 127;
            }
            const int wx = fx & 127;
            const uint8_t *p = r0 + sx * 3, *q = r1 + sx * 3;
            for (int ch = 0; ch < 3; ch++) {
                const int t = p[ch] * (128 - wx) + p[ch + 3] * wx;
                const int b = q[ch] * (128 - wx) + q[ch + 3] * wx;
                o[ch] = (uint8_t)((t * (128 - wy) + b * wy + 8192) >> 14);
            }
        }
    }
}

void fx_up2_rgb(const uint8_t *src, int sw, int sh, fx_rgb_t *dst) {
    up2_ctx_t k = {src, sw, sh, dst};
    fx_par_rows(up2_rows, &k, dst->h);
}

// ---------------------------------------------------------------- Кувахара
// Каждый пиксель берёт средний цвет того из четырёх соседних квадратов, где
// разброс яркости меньше всего. Края остаются резкими, а текстура внутри
// областей (листва, волны, кожа) сглаживается в ровные мазки. Средние по
// квадратам считаются обычным размытием окном, поэтому цена не зависит от
// радиуса.
typedef struct {
    uint8_t *H;
    const uint8_t *M, *mL, *mL2;
    int w, h, rq;
} kuw_ctx_t;

static void kuw_rows(void *c, int y0, int y1) {
    kuw_ctx_t *k = c;
    const int w = k->w, h = k->h, rq = k->rq;
    for (int y = y0; y < y1; y++) {
        const int ya = y - rq < 0 ? 0 : y - rq, yb = y + rq >= h ? h - 1 : y + rq;
        uint8_t *o = k->H + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, o += 3) {
            const int xa = x - rq < 0 ? 0 : x - rq, xb = x + rq >= w ? w - 1 : x + rq;
            const int cand[4] = {ya * w + xa, ya * w + xb, yb * w + xa, yb * w + xb};
            int best = 0, bv = 1 << 30;
            for (int q = 0; q < 4; q++) {
                const int m = k->mL[cand[q]];
                const int v = (k->mL2[cand[q]] << 8) - m * m;
                if (v < bv) {
                    bv = v;
                    best = cand[q];
                }
            }
            const uint8_t *s = k->M + (size_t)best * 3;
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
        }
    }
}

void fx_kuwahara(uint8_t *H, int w, int h, int rq, uint8_t *M, uint8_t *mL, uint8_t *mL2) {
    const int n = w * h;
    for (int i = 0; i < n; i++) {
        const int l = fx_luma(H + (size_t)i * 3);
        mL[i] = (uint8_t)l;
        mL2[i] = (uint8_t)((l * l + 128) >> 8);
    }
    memcpy(M, H, (size_t)n * 3);
    fx_blur_plane(M, w, h, 3, rq);
    fx_blur_plane(mL, w, h, 1, rq);
    fx_blur_plane(mL2, w, h, 1, rq);
    kuw_ctx_t k = {H, M, mL, mL2, w, h, rq};
    fx_par_rows(kuw_rows, &k, h);
}

// ---------------------------------------------------------------- XDoG
// Разность двух гауссиан (σ и 1.6σ) выделяет перепады яркости. Тёмная сторона
// перепада, где разность ниже -t, становится линией. Это чище Собеля:
// линия ложится по краю формы, а не обводит каждую травинку.
typedef struct {
    uint8_t *A;
    const uint8_t *B;
    int w, t;
} xdog_ctx_t;

static void xdog_rows(void *c, int y0, int y1) {
    xdog_ctx_t *k = c;
    for (int y = y0; y < y1; y++) {
        uint8_t *a = k->A + (size_t)y * k->w;
        const uint8_t *b = k->B + (size_t)y * k->w;
        for (int x = 0; x < k->w; x++) a[x] = (a[x] - b[x]) < -k->t ? 1 : 0;
    }
}

void fx_xdog(const fx_rgb_t *im, uint8_t *A, uint8_t *B, int r1, int r2, int t) {
    const int w = im->w, h = im->h;
    fx_luma_plane(im, A);
    memcpy(B, A, (size_t)w * h);
    fx_blur_plane(A, w, h, 1, r1);
    fx_blur_plane(A, w, h, 1, r1);
    fx_blur_plane(B, w, h, 1, r2);
    fx_blur_plane(B, w, h, 1, r2);
    xdog_ctx_t k = {A, B, w, t};
    fx_par_rows(xdog_rows, &k, h);
}

// Чистка маски 0/1: точка остаётся, если рядом минимум keep соседей;
// grow=1 дополнительно утолщает линию на пиксель. tmp: 3 строки.
void fx_mask_clean(uint8_t *m, int w, int h, int keep, int grow) {
    uint8_t *ring = malloc((size_t)w * 3);
    if (!ring) return;
    // Исходные строки y-1, y, y+1 хранятся отдельно: строку y уже перезаписываем.
    uint8_t *prev = ring, *cur = ring + w, *next = ring + 2 * w;
    memcpy(prev, m, (size_t)w);
    memcpy(cur, m, (size_t)w);
    for (int y = 0; y < h; y++, prev = cur, cur = next, next = ring + ((next - ring) + w) % (3 * w)) {
        memcpy(next, m + (size_t)(y + 1 < h ? y + 1 : y) * w, (size_t)w);
        uint8_t *o = m + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const int xl = x ? x - 1 : 0, xr = x < w - 1 ? x + 1 : x;
            const int nb = prev[xl] + prev[x] + prev[xr] + cur[xl] + cur[xr] + next[xl] + next[x] + next[xr];
            if (cur[x])
                o[x] = nb >= keep;
            else
                o[x] = grow && nb >= 3;
        }
    }
    free(ring);
}

void fx_bits_pack(const uint8_t *m, int n, uint8_t *bits) {
    memset(bits, 0, (size_t)(n + 7) / 8);
    for (int i = 0; i < n; i++)
        if (m[i]) bits[i >> 3] |= (uint8_t)(1u << (i & 7));
}

// ---------------------------------------------------------------- свечение
// Яркое выше порога уменьшается вчетверо, сильно размывается и
// возвращается «экраном»: светлые места расплываются белым ореолом.
void fx_bloom(fx_rgb_t *im, int thr, int gain_q8, int radius, uint8_t *tmp) {
    const int w = im->w, h = im->h, qw = w / 4, qh = h / 4;
    uint8_t *Q = tmp;
    for (int qy = 0; qy < qh; qy++)
        for (int qx = 0; qx < qw; qx++) {
            int s = 0;
            for (int yy = 0; yy < 4; yy++) {
                const uint8_t *p = im->p + ((size_t)(qy * 4 + yy) * w + qx * 4) * 3;
                for (int xx = 0; xx < 4; xx++, p += 3) s += fx_luma(p);
            }
            Q[qy * qw + qx] = fx_u8(((s >> 4) - thr) * gain_q8 >> 8);
        }
    for (int i = 0; i < 3; i++) fx_blur_plane(Q, qw, qh, 1, radius);
    for (int y = 0; y < h; y++) {
        const int qy = FX_MIN(y / 4, qh - 1);
        const int qy2 = FX_MIN(qy + 1, qh - 1), wy = (y & 3) * 64;
        uint8_t *d = im->p + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, d += 3) {
            const int qx = FX_MIN(x / 4, qw - 1), qx2 = FX_MIN(qx + 1, qw - 1), wx = (x & 3) * 64;
            const int t = Q[qy * qw + qx] * (256 - wx) + Q[qy * qw + qx2] * wx;
            const int b = Q[qy2 * qw + qx] * (256 - wx) + Q[qy2 * qw + qx2] * wx;
            const int g = (t * (256 - wy) + b * wy) >> 16;
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)(d[c] + (((255 - d[c]) * g) >> 8));
        }
    }
}

// ---------------------------------------------------------------- каналы
// Радиальный разъезд: канал ch растягивается от центра в s раз (s > 1:
// цветная кайма снаружи, s < 1: внутри), как у дешёвого объектива.
void fx_ca_channel(fx_rgb_t *im, int ch, float s, uint8_t *plane) {
    const int w = im->w, h = im->h;
    for (int i = 0; i < w * h; i++) plane[i] = im->p[(size_t)i * 3 + ch];
    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f, inv = 1.0f / s;
    for (int y = 0; y < h; y++) {
        const float sy = cy + ((float)y - cy) * inv;
        int y0 = (int)sy;
        const int wy = (int)((sy - (float)y0) * 256.0f);
        if (y0 < 0) y0 = 0;
        if (y0 > h - 2) y0 = h - 2;
        uint8_t *d = im->p + (size_t)y * w * 3 + ch;
        const uint8_t *r0 = plane + (size_t)y0 * w, *r1 = r0 + w;
        for (int x = 0; x < w; x++, d += 3) {
            const float sx = cx + ((float)x - cx) * inv;
            int x0 = (int)sx;
            const int wx = (int)((sx - (float)x0) * 256.0f);
            if (x0 < 0) x0 = 0;
            if (x0 > w - 2) x0 = w - 2;
            const int t = r0[x0] * (256 - wx) + r0[x0 + 1] * wx;
            const int b = r1[x0] * (256 - wx) + r1[x0 + 1] * wx;
            *d = (uint8_t)((t * (256 - wy) + b * wy) >> 16);
        }
    }
}

// ---------------------------------------------------------------- закрутка
// Воронка вокруг (cx, cy): угол поворота падает от amount в центре до нуля
// на радиусе R. Нужна копия квадрата 2R x 2R (tmp).
void fx_swirl(fx_rgb_t *im, int cx, int cy, int R, float amount, uint8_t *tmp, size_t tmp_size) {
    const int w = im->w, h = im->h;
    while ((size_t)(2 * R) * (2 * R) * 3 > tmp_size && R > 8) R -= 8;
    const int bx0 = FX_MAX(cx - R, 0), by0 = FX_MAX(cy - R, 0);
    const int bx1 = FX_MIN(cx + R, w), by1 = FX_MIN(cy + R, h);
    const int bw = bx1 - bx0, bh = by1 - by0;
    if (bw < 4 || bh < 4) return;
    for (int y = 0; y < bh; y++)
        memcpy(tmp + (size_t)y * bw * 3, im->p + ((size_t)(by0 + y) * w + bx0) * 3, (size_t)bw * 3);
    const float fR = (float)R;
    for (int y = by0; y < by1; y++) {
        uint8_t *d = im->p + ((size_t)y * w + bx0) * 3;
        for (int x = bx0; x < bx1; x++, d += 3) {
            const float dx = (float)(x - cx), dy = (float)(y - cy);
            const float r = sqrtf(dx * dx + dy * dy);
            if (r >= fR) continue;
            const float f = 1.0f - r / fR;
            const float a = amount * f * f;
            const float ca = cosf(a), sa = sinf(a);
            const float sx = (float)cx + dx * ca - dy * sa - (float)bx0;
            const float sy = (float)cy + dx * sa + dy * ca - (float)by0;
            int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
            if (x0 < 0 || y0 < 0 || x0 >= bw - 1 || y0 >= bh - 1) continue;
            const int wx = (int)((sx - (float)x0) * 256.0f), wy = (int)((sy - (float)y0) * 256.0f);
            const uint8_t *p = tmp + ((size_t)y0 * bw + x0) * 3, *q = p + (size_t)bw * 3;
            for (int c = 0; c < 3; c++) {
                const int t = p[c] * (256 - wx) + p[c + 3] * wx;
                const int b = q[c] * (256 - wx) + q[c + 3] * wx;
                d[c] = (uint8_t)((t * (256 - wy) + b * wy) >> 16);
            }
        }
    }
}

// ---------------------------------------------------------------- зерно
typedef struct {
    fx_rgb_t *im;
    uint32_t seed;
    int amount, vig;
} grain_ctx_t;

static void grain_rows(void *c, int y0, int y1) {
    grain_ctx_t *k = c;
    const int w = k->im->w, h = k->im->h, cx = w / 2, cy = h / 2;
    const int64_t rr = (int64_t)cx * cx + (int64_t)cy * cy;
    for (int y = y0; y < y1; y++) {
        uint32_t rng = k->seed ^ (uint32_t)(y * 0x9E3779B1u);
        fx_rand(&rng);
        uint8_t *d = k->im->p + (size_t)y * w * 3;
        const int64_t dy2 = (int64_t)(y - cy) * (y - cy);
        for (int x = 0; x < w; x++, d += 3) {
            const int g = k->amount ? (int)(fx_rand(&rng) % (uint32_t)(2 * k->amount + 1)) - k->amount : 0;
            const int v = k->vig ? 256 - (int)((((int64_t)(x - cx) * (x - cx) + dy2) * k->vig) / rr) : 256;
            for (int ch = 0; ch < 3; ch++) d[ch] = fx_u8(((d[ch] * v) >> 8) + g);
        }
    }
}

void fx_grain(fx_rgb_t *im, uint32_t seed, int amount, int vignette) {
    grain_ctx_t k = {im, seed, amount, vignette};
    fx_par_rows(grain_rows, &k, im->h);
}

// ---------------------------------------------------------------- карты
void fx_gradient_lut(uint8_t lut[256][3], const fx_stop_t *st, int n) {
    for (int i = 0; i < n - 1; i++) {
        const fx_stop_t *a = &st[i], *b = &st[i + 1];
        for (int v = a->v; v <= b->v; v++) {
            const int f = (v - a->v) * 256 / FX_MAX(b->v - a->v, 1);
            for (int c = 0; c < 3; c++) lut[v][c] = (uint8_t)((a->c[c] * (256 - f) + b->c[c] * f) >> 8);
        }
    }
}

// Самое яркое крупное место кадра (по уменьшенной копии): туда ставятся
// воронка, прицелы и кольца, чтобы графика «видела» сюжет.
void fx_find_hotspot(const fx_rgb_t *im, int *ox, int *oy) {
    enum { GW = 32, GH = 24 };
    int g[GH][GW];
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            int s = 0;
            for (int k = 0; k < 4; k++) {
                const int sx = (x * im->w / GW) + (k & 1) * im->w / GW / 2;
                const int sy = (y * im->h / GH) + (k >> 1) * im->h / GH / 2;
                s += fx_luma(im->p + ((size_t)sy * im->w + sx) * 3);
            }
            g[y][x] = s;
        }
    int best = -1;
    *ox = im->w / 2;
    *oy = im->h / 2;
    for (int y = 2; y < GH - 2; y++)
        for (int x = 2; x < GW - 2; x++) {
            // яркость с контрастом к окрестности, чтобы не выбирать пустое небо
            int s = 0, ring = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) s += g[y + dy][x + dx];
            for (int dx = -2; dx <= 2; dx++) ring += g[y - 2][x + dx] + g[y + 2][x + dx];
            const int score = s * 2 - ring * 9 / 10;
            if (score > best) {
                best = score;
                *ox = x * im->w / GW + im->w / GW / 2;
                *oy = y * im->h / GH + im->h / GH / 2;
            }
        }
}

// Строка горизонта: где сильнее всего меняется яркость по вертикали.
int fx_find_horizon(const uint8_t *L, int w, int h) {
    int best = h / 2;
    long bv = -1;
    for (int y = h / 5; y < h * 4 / 5; y += 2) {
        long s = 0;
        const uint8_t *a = L + (size_t)(y - 3) * w, *b = L + (size_t)(y + 3) * w;
        for (int x = 0; x < w; x += 4) s += FX_ABS((int)a[x] - (int)b[x]);
        if (s > bv) {
            bv = s;
            best = y;
        }
    }
    return best;
}

// ---------------------------------------------------------------- рисование
void fx_blend_rect(fx_rgb_t *im, int x, int y, int w, int h, const uint8_t c[3], int a) {
    const int x0 = FX_MAX(x, 0), y0 = FX_MAX(y, 0), x1 = FX_MIN(x + w, im->w), y1 = FX_MIN(y + h, im->h);
    for (int yy = y0; yy < y1; yy++) {
        uint8_t *d = im->p + ((size_t)yy * im->w + x0) * 3;
        for (int xx = x0; xx < x1; xx++, d += 3)
            for (int ch = 0; ch < 3; ch++) d[ch] = (uint8_t)((d[ch] * (256 - a) + c[ch] * a) >> 8);
    }
}

void fx_frame_rect(fx_rgb_t *im, int x, int y, int w, int h, int t, const uint8_t c[3]) {
    fx_fill_rect(im, x, y, w, t, c);
    fx_fill_rect(im, x, y + h - t, w, t, c);
    fx_fill_rect(im, x, y, t, h, c);
    fx_fill_rect(im, x + w - t, y, t, h, c);
}

void fx_text_outlined(fx_rgb_t *im, int x, int y, int scale, const char *s, const uint8_t fg[3], const uint8_t ol[3]) {
    const int o = FX_MAX(1, scale / 2);
    for (int dy = -o; dy <= o; dy += o)
        for (int dx = -o; dx <= o; dx += o)
            if (dx || dy) fx_text(im, x + dx, y + dy, scale, s, ol);
    fx_text(im, x, y, scale, s, fg);
}

void fx_shot_label(char *buf, uint32_t shot) {
    const unsigned s = (unsigned)(shot % 10000u);
    buf[0] = '#';
    buf[1] = (char)('0' + s / 1000);
    buf[2] = (char)('0' + s / 100 % 10);
    buf[3] = (char)('0' + s / 10 % 10);
    buf[4] = (char)('0' + s % 10);
    buf[5] = 0;
}
