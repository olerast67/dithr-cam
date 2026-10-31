// SHARD: кадр разбит на осколки (ячейки Вороного). Центров больше там,
// где много деталей, поэтому лицо или листва дробятся мельче неба.
//  GLASS   каждый осколок преломляет картинку со своим сдвигом и бликом,
//          грани светятся
//  MOSAIC  витраж: ровный насыщенный цвет осколка, тёмный свинцовый шов
//  PRISM   у каждого осколка каналы R и B разведены в свою сторону: радуга
//
// Память: номер ячейки на пиксель (uint16) занимает scratch[0, 2n); кольцо
// исходных строк для преломления лежит в хвосте scratch.
#include <math.h>

#include "fx_internal.h"

enum { V_GLASS, V_MOSAIC, V_PRISM };

#define MAXC 1400
#define G 40  // ячейка сетки поиска (поиск 5x5 = ±80 px)
#define OFS 8 // максимальный сдвиг преломления, пикселей

typedef struct {
    int16_t x, y;
    int8_t dx, dy;   // преломление
    int16_t shade;   // наклон грани: -64..64
    uint8_t col[3];  // средний цвет (MOSAIC)
} cell_t;

typedef struct {
    fx_job_t *j;
    cell_t *c;
    int nc;
    uint16_t *id;
    uint8_t *ebits;   // швы между осколками, 1 бит на пиксель
    int16_t *bstart;  // начало списка ячеек каждого квадрата сетки
    int16_t *blist;
    int gw, gh;
} shard_ctx_t;

// Ближайший и второй ближайший центр через сетку 5x5 квадратов.
static void voronoi_rows(void *c, int y0, int y1) {
    shard_ctx_t *k = c;
    const int w = k->j->img.w;
    for (int y = y0; y < y1; y++) {
        const int gy = y / G;
        for (int x = 0; x < w; x++) {
            const int gx = x / G;
            int b1 = 1 << 30, b2 = 1 << 30, best = 0;
            for (int yy = FX_MAX(gy - 2, 0); yy <= FX_MIN(gy + 2, k->gh - 1); yy++)
                for (int xx = FX_MAX(gx - 2, 0); xx <= FX_MIN(gx + 2, k->gw - 1); xx++) {
                    const int bi = yy * k->gw + xx;
                    for (int q = k->bstart[bi]; q < k->bstart[bi + 1]; q++) {
                        const cell_t *s = &k->c[k->blist[q]];
                        const int dx = s->x - x, dy = s->y - y, d = dx * dx + dy * dy;
                        if (d < b1) {
                            b2 = b1;
                            b1 = d;
                            best = k->blist[q];
                        } else if (d < b2) {
                            b2 = d;
                        }
                    }
                }
            k->id[(size_t)y * w + x] = (uint16_t)best;
            // шов: точка почти на равном расстоянии от двух центров
            const float d1 = sqrtf((float)b1), d2 = sqrtf((float)b2);
            if ((d2 - d1) < 1.3f) {
                const size_t i = (size_t)y * w + x;
                k->ebits[i >> 3] |= (uint8_t)(1u << (i & 7));
            }
        }
    }
}

void fx_shard_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    const int w = im->w, h = im->h, n = w * h;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    const int v = j->variant % 3;

    // 1. Карта деталей (Собель по размытой яркости) в первой половине scratch
    uint8_t *L = j->scratch, *E = j->scratch + n;
    fx_luma_plane(im, L);
    fx_blur_plane(L, w, h, 1, 2);
    fx_sobel(L, E, w, h);
    fx_blur_plane(E, w, h, 1, 6);

    // 2. Центры осколков: случайные точки, принятые с вероятностью по деталям
    shard_ctx_t k = {.j = j};
    k.c = malloc(sizeof(cell_t) * MAXC);
    k.gw = (w + G - 1) / G;
    k.gh = (h + G - 1) / G;
    k.bstart = calloc((size_t)k.gw * k.gh + 1, sizeof(int16_t));
    k.blist = malloc(sizeof(int16_t) * MAXC);
    uint8_t *edge_bits = NULL;
    if (!k.c || !k.bstart || !k.blist) goto out;
    // Сначала равномерная подложка (сетка с разбросом), чтобы у каждой точки
    // кадра был центр в радиусе поиска, потом центры по деталям.
    const int sp = 56;
    for (int gy = sp / 2; gy < h && k.nc < MAXC; gy += sp)
        for (int gx = sp / 2; gx < w && k.nc < MAXC; gx += sp) {
            cell_t *s = &k.c[k.nc++];
            s->x = (int16_t)fx_clampi(gx + fx_rand_range(&rng, -sp / 3, sp / 3), 0, w - 1);
            s->y = (int16_t)fx_clampi(gy + fx_rand_range(&rng, -sp / 3, sp / 3), 0, h - 1);
            s->dx = (int8_t)fx_rand_range(&rng, -OFS, OFS);
            s->dy = (int8_t)fx_rand_range(&rng, -OFS, OFS);
            s->shade = (int16_t)fx_rand_range(&rng, -60, 60);
        }
    const int target = k.nc + fx_rand_range(&rng, 350, 900);
    for (int tries = 0; k.nc < target && tries < target * 30; tries++) {
        const int x = fx_rand_range(&rng, 0, w - 1), y = fx_rand_range(&rng, 0, h - 1);
        const int e = E[(size_t)y * w + x];
        if ((int)(fx_rand(&rng) & 255) > 22 + e * 3) continue;
        cell_t *s = &k.c[k.nc++];
        s->x = (int16_t)x;
        s->y = (int16_t)y;
        s->dx = (int8_t)fx_rand_range(&rng, -OFS, OFS);
        s->dy = (int8_t)fx_rand_range(&rng, -OFS, OFS);
        s->shade = (int16_t)fx_rand_range(&rng, -60, 60);
    }
    // раскладка по квадратам сетки (подсчётом)
    for (int i = 0; i < k.nc; i++) k.bstart[(k.c[i].y / G) * k.gw + k.c[i].x / G + 1]++;
    for (int i = 0; i < k.gw * k.gh; i++) k.bstart[i + 1] += k.bstart[i];
    {
        int16_t *fillp = calloc((size_t)k.gw * k.gh, sizeof(int16_t));
        if (!fillp) goto out;
        for (int i = 0; i < k.nc; i++) {
            const int bi = (k.c[i].y / G) * k.gw + k.c[i].x / G;
            k.blist[k.bstart[bi] + fillp[bi]++] = (int16_t)i;
        }
        free(fillp);
    }
    fx_progress(j, 25);

    // 3. Номер ячейки на пиксель (весь [0, 2n)) и швы в битах. При ширине,
    // кратной 8, каждая строка битов занимает свои байты: ядра не мешают друг другу.
    k.id = (uint16_t *)j->scratch;
    edge_bits = calloc((size_t)(n + 7) / 8, 1);
    if (!edge_bits) goto out;
    k.ebits = edge_bits;
    if (w % 8 == 0)
        fx_par_rows(voronoi_rows, &k, h);
    else
        voronoi_rows(&k, 0, h);

    // 4. Средний цвет осколка (для витража)
    if (v == V_MOSAIC) {
        int32_t *sum = calloc((size_t)k.nc * 4, sizeof(int32_t));
        if (!sum) goto out;
        for (int i = 0; i < n; i++) {
            int32_t *s = sum + (size_t)k.id[i] * 4;
            const uint8_t *p = im->p + (size_t)i * 3;
            s[0] += p[0];
            s[1] += p[1];
            s[2] += p[2];
            s[3]++;
        }
        for (int c = 0; c < k.nc; c++) {
            const int32_t *s = sum + (size_t)c * 4;
            const int cnt = FX_MAX(s[3], 1);
            const int r = s[0] / cnt, g = s[1] / cnt, b = s[2] / cnt, l = (77 * r + 150 * g + 29 * b) >> 8;
            // витражное стекло насыщеннее и чуть светлее
            k.c[c].col[0] = fx_u8(l + (r - l) * 9 / 5 + 12);
            k.c[c].col[1] = fx_u8(l + (g - l) * 9 / 5 + 12);
            k.c[c].col[2] = fx_u8(l + (b - l) * 9 / 5 + 12);
        }
        free(sum);
    }
    fx_progress(j, 70);

    // 5. Сборка по строкам. Для преломления нужны исходные строки y-OFS..y+OFS:
    // держим их кольцом в хвосте scratch (2*OFS+2 строк по w*3 байт).
    {
        const int R = 2 * OFS + 2;
        uint8_t *ring = j->scratch + 2 * (size_t)n;
        if ((size_t)R * w * 3 > 65536) goto out;
        for (int y = 0; y <= OFS && y < h; y++) memcpy(ring + (size_t)(y % R) * w * 3, im->p + (size_t)y * w * 3, (size_t)w * 3);
        static const uint8_t SEAM_G[3] = {230, 250, 255}, SEAM_M[3] = {16, 12, 20};
        for (int y = 0; y < h; y++) {
            if (y + OFS + 1 < h)
                memcpy(ring + (size_t)((y + OFS + 1) % R) * w * 3, im->p + (size_t)(y + OFS + 1) * w * 3, (size_t)w * 3);
            uint8_t *d = im->p + (size_t)y * w * 3;
            for (int x = 0; x < w; x++, d += 3) {
                const int i = y * w + x;
                const cell_t *c = &k.c[k.id[i]];
                if ((edge_bits[i >> 3] >> (i & 7)) & 1) {
                    if (v == V_MOSAIC) {
                        memcpy(d, SEAM_M, 3);
                    } else {  // грань стекла: светлый полупрозрачный блик
                        const uint8_t *o = ring + ((size_t)(y % R) * w + x) * 3;
                        for (int ch = 0; ch < 3; ch++) d[ch] = (uint8_t)((o[ch] * 90 + SEAM_G[ch] * 166) >> 8);
                    }
                    continue;
                }
                if (v == V_MOSAIC) {
                    // лёгкий градиент внутри стекла
                    const int g = ((x - c->x) + (y - c->y)) * 2;
                    for (int ch = 0; ch < 3; ch++) d[ch] = fx_u8(c->col[ch] + g / 8);
                    continue;
                }
                const int sx = fx_clampi(x + c->dx, 0, w - 1);
                const int sy = fx_clampi(y + c->dy, FX_MAX(y - OFS, 0), FX_MIN(y + OFS, h - 1));
                const uint8_t *src = ring + ((size_t)(sy % R) * w + sx) * 3;
                // блик по наклону грани: ярче к одной стороне осколка
                const int sh = c->shade * ((x - c->x) - (y - c->y)) / 64;
                if (v == V_PRISM) {
                    const int px = fx_clampi(x + c->dx * 2, 0, w - 1), mx = fx_clampi(x - c->dx, 0, w - 1);
                    const uint8_t *sr = ring + ((size_t)(sy % R) * w + px) * 3;
                    const uint8_t *sb = ring + ((size_t)(sy % R) * w + mx) * 3;
                    d[0] = fx_u8(sr[0] + sh);
                    d[1] = fx_u8(src[1] + sh);
                    d[2] = fx_u8(sb[2] + sh);
                } else {
                    for (int ch = 0; ch < 3; ch++) d[ch] = fx_u8(src[ch] + sh + 6);
                }
            }
        }
    }
    fx_progress(j, 90);
    if (v != V_MOSAIC) fx_bloom(im, 200, 500, FX_MAX(2, w / 260), j->scratch);
    fx_grain(im, j->seed, 4, 60);
    goto done;
out:
    j->failed = true;
done:
    free(k.c);
    free(k.bstart);
    free(k.blist);
    free(edge_bits);
}
