// «Тёмная комната» для ПК: прогоняет снимок через тот же путь, что камера.
// JPEG 1600x1200 -> декодер прошивки (codec.c + JPEGDEC) -> 800x600 -> эффект.
//
//   darkroom STYLE VARIANT SEED OUT IN1.jpg [IN2.jpg IN3.jpg]
//     VARIANT: номер варианта или -1 (случайный по SEED)
//     OUT: .ppm для цветных стилей, .png для DMG и INK
//   darkroom --preview STYLE FRAMES OUT.pgm IN.jpg...   превью 128x96 x4
//
// Собирать 32-битным (как ESP32), см. host/run.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "codec.h"
#include "fx.h"

#define W 800
#define H 600

static uint8_t *load(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

static int file_write(void *user, const void *d, size_t n) { return fwrite(d, 1, n, (FILE *)user) == n ? 0 : -1; }

static int style_by_name(const char *s) {
    for (int i = 0; i < FX_COUNT; i++)
        if (strcmp(fx_info((fx_style_t)i)->name, s) == 0) return i;
    return -1;
}

static int do_shot(int argc, char **argv) {
    const int st = style_by_name(argv[1]);
    if (st < 0) return fprintf(stderr, "unknown style\n"), 2;
    const int want = atoi(argv[2]);
    const uint32_t seed = (uint32_t)strtoul(argv[3], NULL, 10);
    const char *out = argv[4];
    size_t n;
    uint8_t *jpg = load(argv[5], &n);
    if (!jpg) return fprintf(stderr, "bad input\n"), 2;

    fx_rgb_t img = {malloc((size_t)W * H * 3), W, H};
    const size_t ss = fx_scratch_size(W, H);
    uint8_t *scratch = malloc(ss);
    if (dec_rgb(jpg, n, &img, 7) != ESP_OK) return fprintf(stderr, "decode failed\n"), 2;

    fx_job_t job = {.img = img, .scratch = scratch, .seed = seed, .shot = 42};
    job.variant = fx_pick_variant((fx_style_t)st, want, seed);

    clock_t t0 = clock();
    // Те же шаги до эффекта, что в прошивке (app_main.c, capture)
    if (st == FX_GHOST) {
        for (int k = 1; k < 3; k++) {
            size_t m;
            uint8_t *f = load(argv[5 + (argc > 5 + k ? k : 0)], &m);
            dec_plane(f, m, &img, k == 1 ? FX_GHOST_G(&job) : FX_GHOST_B(&job), k);
            free(f);
        }
    }
    if (st == FX_MOSH && job.variant == FX_MOSH_CORRUPT) dec_rgb_glitch(jpg, n, &img, seed, scratch, ss);
    if (st == FX_TIDE) {
        // как прошивка: карта времени, затем кадр k только в свои пиксели
        fx_tide_map(job.variant, seed, scratch, W, H);
        const int nf = argc - 5;
        for (int k = 0; k < FX_TIDE_FRAMES; k++) {
            size_t m;
            uint8_t *f = load(argv[5 + k * nf / FX_TIDE_FRAMES], &m);
            dec_rgb_tmap(f, m, &img, scratch, k);
            free(f);
        }
    }

    fx_render((fx_style_t)st, &job);
    const double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("%s/%s %.0f ms\n", fx_info((fx_style_t)st)->name, fx_info((fx_style_t)st)->var[job.variant], ms);

    FILE *f = fopen(out, "wb");
    if (!f) return 2;
    if (job.idx) {
        fx_png_write_indexed(file_write, f, job.idx, job.out_w, job.out_h, job.bpp, (const uint8_t(*)[3])job.pal,
                             job.pal_n);
    } else {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(img.p, 1, (size_t)W * H * 3, f);
    }
    fclose(f);
    return 0;
}

static int do_preview(int argc, char **argv) {
    const int st = style_by_name(argv[2]);
    const int frames = atoi(argv[3]);
    const char *out = argv[4];
    const int nin = argc - 5;
    static fx_prev_t ps;
    fx_preview_init(&ps);
    static uint8_t g160[160 * 120], g[FX_PN], m[FX_PN];
    for (int f = 0; f < frames; f++) {
        size_t n;
        uint8_t *jpg = load(argv[5 + f % nin], &n);
        if (!jpg) return 2;
        // как камера: превью из JPEG 320x240 с уменьшением вдвое
        if (dec_gray_half(jpg, n, g160, 160, 120) != ESP_OK) return 2;
        free(jpg);
        fx_resize_gray(g160, 160, 120, g, FX_PW, FX_PH);
        fx_preview((fx_style_t)st, &ps, g, m);
    }
    const int S = 4;
    FILE *fo = fopen(out, "wb");
    fprintf(fo, "P5\n%d %d\n255\n", FX_PW * S, FX_PH * S);
    for (int y = 0; y < FX_PH * S; y++)
        for (int x = 0; x < FX_PW * S; x++) fputc(m[(y / S) * FX_PW + x / S] ? 235 : 12, fo);
    fclose(fo);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 6 && strcmp(argv[1], "--preview") == 0) return do_preview(argc, argv);
    if (argc >= 6) return do_shot(argc, argv);
    fprintf(stderr, "usage: see header\n");
    return 1;
}
