// MOSH: сбой цифрового и аналогового видео.
//  CRPT   настоящий датамош: прошивка уже вписала в кадр полосы из JPEG с
//         испорченными битами (dec_rgb_glitch). Здесь лёгкая доводка.
//  BLOCK  P-кадры без опорного кадра: макроблоки тянутся по векторам
//         движения, цвет схлопывается в блоки 16x16.


#include "fx_internal.h"

enum { V_CRPT, V_BLOCK };
#define BS 16

int fx_mosh_bands(uint32_t seed, int h, int bands[][2], int max) {
    uint32_t rng = seed ^ 0x5EED5EEDu;
    fx_rand(&rng);
    const int n = FX_MIN(max, fx_rand_range(&rng, 2, 4));
    for (int i = 0; i < n; i++) {
        const int y0 = (h * (i * 2 + 1) / (2 * n + 1)) + fx_rand_range(&rng, -h / 20, h / 20);
        bands[i][0] = fx_clampi(y0, 0, h - 1);
        bands[i][1] = fx_clampi(y0 + fx_rand_range(&rng, h / 12, h / 4), 1, h);
    }
    return n;
}

// ---------------------------------------------------------------- BLOCK
static void block_copy(fx_rgb_t *im, int sx, int sy, int dx, int dy) {
    const int w = im->w;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + BS > w || dx + BS > w || sy + BS > im->h || dy + BS > im->h)
        return;
    for (int y = 0; y < BS; y++)
        memmove(im->p + ((size_t)(dy + y) * w + dx) * 3, im->p + ((size_t)(sy + y) * w + sx) * 3, BS * 3);
}

static void block_flat(fx_rgb_t *im, int bx, int by, int chroma_only) {
    const int w = im->w;
    int s[3] = {0, 0, 0}, sl = 0;
    for (int y = 0; y < BS; y++) {
        const uint8_t *p = im->p + ((size_t)(by + y) * w + bx) * 3;
        for (int x = 0; x < BS; x++, p += 3) {
            const int l = fx_luma(p);
            sl += l;
            for (int c = 0; c < 3; c++) s[c] += p[c] - (chroma_only ? l : 0);
        }
    }
    for (int y = 0; y < BS; y++) {
        uint8_t *p = im->p + ((size_t)(by + y) * w + bx) * 3;
        for (int x = 0; x < BS; x++, p += 3) {
            const int l = chroma_only ? fx_luma(p) : 0;
            for (int c = 0; c < 3; c++) p[c] = fx_u8(l + s[c] / (BS * BS));
        }
    }
    (void)sl;
}

static void blocks(fx_rgb_t *im, uint32_t *rng) {
    const int w = im->w, h = im->h, bw = w / BS, bh = h / BS;
    // Области с общим вектором движения; блоки «размазываются» по вектору
    const int regions = fx_rand_range(rng, 3, 6);
    for (int r = 0; r < regions; r++) {
        const int rx = fx_rand_range(rng, 0, bw - 4), ry = fx_rand_range(rng, 0, bh - 4);
        const int rw = fx_rand_range(rng, 3, bw / 3), rh = fx_rand_range(rng, 2, bh / 3);
        const int mvx = fx_rand_range(rng, -1, 1), mvy = fx_rand_range(rng, 0, 1) ? 1 : -1;
        const int steps = fx_rand_range(rng, 3, 9);
        for (int s = 0; s < steps; s++)
            for (int by = 0; by < rh; by++)
                for (int bx = 0; bx < rw; bx++) {
                    const int x = (rx + bx) * BS, y = (ry + by) * BS;
                    block_copy(im, x, y, x + mvx * BS * (s + 1) / 2, y + mvy * BS * (s + 1) / 2);
                }
    }
    // Схлопнутый цвет (как 4:2:0 на последнем издыхании) и плоские блоки
    const int nf = fx_rand_range(rng, 60, 160);
    for (int i = 0; i < nf; i++)
        block_flat(im, fx_rand_range(rng, 0, bw - 1) * BS, fx_rand_range(rng, 0, bh - 1) * BS,
                   (fx_rand(rng) & 3) != 0);
}

void fx_mosh_render(fx_job_t *j) {
    fx_rgb_t *im = &j->img;
    uint32_t rng = j->seed;
    fx_rand(&rng);
    switch (j->variant) {
        case V_BLOCK: blocks(im, &rng); break;
        default: {
            // CRPT: полосы уже испорчены декодером; добавим немного тяги блоков
            const int nd = fx_rand_range(&rng, 2, 5);
            for (int i = 0; i < nd; i++) {
                const int x = fx_rand_range(&rng, 0, im->w / BS - 3) * BS, y = fx_rand_range(&rng, 0, im->h / BS - 4) * BS;
                for (int s = 1; s < 8; s++) block_copy(im, x, y, x, y + s * BS / 2);
            }
            break;
        }
    }
    fx_progress(j, 80);
    fx_grain(im, j->seed, 4, 0);
}
