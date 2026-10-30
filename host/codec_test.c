// Проверка декодеров прошивки (main/codec.c) на ПК на настоящих JPEG.
//   codec_test prev.jpg uxga.jpg OUTDIR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "codec.h"

static uint8_t *load(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    fread(b, 1, *n, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    size_t n1, n2;
    uint8_t *a = load(argv[1], &n1), *b = load(argv[2], &n2);
    int w, h;
    if (!jpeg_dims(a, n1, &w, &h)) return printf("dims fail 1\n"), 1;
    printf("preview dims %dx%d\n", w, h);
    if (!jpeg_dims(b, n2, &w, &h)) return printf("dims fail 2\n"), 1;
    printf("capture dims %dx%d\n", w, h);

    static uint8_t g[160 * 120];
    memset(g, 7, sizeof(g));
    clock_t t0 = clock();
    for (int i = 0; i < 50; i++)
        if (dec_gray_half(a, n1, g, 160, 120) != ESP_OK) return printf("gray fail\n"), 1;
    printf("gray half: %.2f ms (x86)\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 50);
    char p[512];
    snprintf(p, sizeof(p), "%s/dec_gray.pgm", argv[3]);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P5\n160 120\n255\n");
    fwrite(g, 1, sizeof(g), f);
    fclose(f);

    fx_rgb_t img = {malloc(800 * 600 * 3), 800, 600};
    memset(img.p, 0, 800 * 600 * 3);
    t0 = clock();
    if (dec_rgb(b, n2, &img, 7) != ESP_OK) return printf("rgb fail\n"), 1;
    printf("rgb half: %.2f ms (x86)\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    snprintf(p, sizeof(p), "%s/dec_rgb.ppm", argv[3]);
    f = fopen(p, "wb");
    fprintf(f, "P6\n800 600\n255\n");
    fwrite(img.p, 1, 800 * 600 * 3, f);
    fclose(f);
    // Маска каналов: только G поверх нулей
    memset(img.p, 0, 800 * 600 * 3);
    dec_rgb(b, n2, &img, 2);
    long sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < 800 * 600; i++) sr += img.p[i * 3], sg += img.p[i * 3 + 1], sb += img.p[i * 3 + 2];
    printf("mask G only: sumR=%ld sumG=%ld sumB=%ld\n", sr, sg, sb);
    return 0;
}
