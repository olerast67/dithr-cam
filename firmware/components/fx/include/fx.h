// fx — ядро эффектов камеры. Чистый C99 без зависимостей от ESP-IDF:
// тот же код собирается в прошивку и в программу для ПК (host/), где
// эффекты можно проверять на обычных фотографиях.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Картинка RGB888, строки подряд, шаг строки w*3 байт.
typedef struct {
    uint8_t *p;
    int w, h;
} fx_rgb_t;

typedef enum {
    FX_NEON,   // аниме-неон: плоские заливки, чернильный контур, фиолетовая обводка
    FX_Y2K,    // пересвет, сине-фиолетовые тени, протяжки пикселей, векторный HUD
    FX_MELT,   // пиксель-сортинг: картинка «стекает» вниз
    FX_GHOST,  // три кадра во времени в каналы R, G, B: движение становится радугой
    FX_MOSH,   // цифровой сбой: сдвиги полос, тянущиеся блоки, разъезд цвета
    FX_DMG,    // Game Boy Camera: 160x120, четыре оттенка зелёного
    FX_INK,    // печать: гравюра, ризограф
    FX_TIDE,   // снимок из времени: каждая часть кадра из своего момента
    FX_PULSE,  // кадр из линий пульса (осциллограф, Unknown Pleasures)
    FX_SHARD,  // кадр разбит на осколки стекла с преломлением
    FX_HOLO,   // объёмная голограмма: облако точек, сетка, скан
    FX_LOOP,   // бесконечная спираль Дросте: кадр уходит сам в себя
    FX_TOPO,   // топографическая карта высот
    FX_MIX,    // каждый снимок в случайном стиле и варианте
    FX_RAW,    // без обработки, сохраняется только исходный JPEG
    FX_COUNT
} fx_style_t;

#define FX_REAL_STYLES 13  // NEON..TOPO: стили, которые реально считают картинку

typedef enum {
    FX_OUT_RGB,   // результат в job->img, прошивка кодирует в JPEG
    FX_OUT_PAL,   // результат в job->idx + палитра, прошивка пишет PNG без потерь
    FX_OUT_NONE,  // обработки нет
} fx_out_t;

typedef struct {
    const char *name;  // 4 символа: для HUD и имени файла
    const char *hint;  // короткое пояснение на экране
    uint8_t frames;    // сколько кадров нужно для снимка (GHOST = 3)
    fx_out_t out;
    uint8_t nvar;              // число вариантов (палитры, компоновки, направления)
    const char *const *var;    // их имена, до 6 символов
} fx_info_t;

const fx_info_t *fx_info(fx_style_t s);

// Вариант для снимка: want >= 0 выбран вручную, иначе случайный по seed.
int fx_pick_variant(fx_style_t s, int want, uint32_t seed);

// ---------------------------------------------------------------- снимок
typedef struct fx_job {
    fx_rgb_t img;          // вход: фото; выход для FX_OUT_RGB
    uint8_t *scratch;      // рабочая память, не меньше fx_scratch_size()
    uint32_t seed;         // случайность эффекта (у каждого снимка своя)
    uint32_t shot;         // номер снимка, рисуется в оверлеях
    int variant;           // номер варианта (из fx_pick_variant)
    // Результат FX_OUT_PAL (idx указывает внутрь scratch), размер out_w x out_h:
    uint8_t *idx;
    int out_w, out_h;
    uint8_t pal[16][3];
    int pal_n;
    int bpp;               // 1, 2, 4 или 8 бит на пиксель в PNG
    // Необязательный отчёт о ходе работы, 0..100.
    void (*progress)(int pct, void *user);
    void *user;
} fx_job_t;

size_t fx_scratch_size(int w, int h);
void fx_render(fx_style_t s, fx_job_t *job);

// GHOST получает три кадра: первый целиком в img, от второго канал G в
// scratch[0..w*h), от третьего канал B в scratch[w*h..2*w*h).
#define FX_GHOST_G(job) ((job)->scratch)
#define FX_GHOST_B(job) ((job)->scratch + (size_t)(job)->img.w * (job)->img.h)

// MOSH, вариант CORRUPT: прошивка декодирует испорченную копию JPEG и
// вписывает её строки только в эти полосы. Полосы зависят от seed, поэтому
// ПК и камера получают одно и то же. Возвращает число полос (y0, y1).
#define FX_MOSH_CORRUPT 0
int fx_mosh_bands(uint32_t seed, int h, int bands[][2], int max);

// TIDE: камера снимает FX_TIDE_FRAMES кадров подряд. Карта времени T
// (w*h байт, лежит в scratch[0..w*h)) говорит, из какого кадра брать пиксель.
// Прошивка строит карту до съёмки и вписывает кадр k только туда, где T == k.
#define FX_TIDE_FRAMES 24
void fx_tide_map(int variant, uint32_t seed, uint8_t *T, int w, int h);

// ---------------------------------------------------------------- превью
// Живое превью: серое 128x96 на входе, на выходе маска 0/1 того же размера
// (1 = пиксель OLED горит).
#define FX_PW 128
#define FX_PH 96
#define FX_PN (FX_PW * FX_PH)

typedef struct {
    uint32_t t;            // номер кадра превью
    uint32_t rng;
    int32_t lo_q8, hi_q8;  // сглаженные уровни автоконтраста (8.8)
    uint8_t hist[4][FX_PN];  // история кадров для GHOST
    uint8_t tide[FX_TIDE_FRAMES][FX_PN];  // история кадров для TIDE (~290 КБ)
    uint8_t tmp[FX_PN];
    uint8_t tmp2[FX_PN];
    // Полосы протяжки и сбоя: y, высота, x0/сдвиг
    int16_t band[4][3];
    uint32_t band_t;
} fx_prev_t;

void fx_preview_init(fx_prev_t *st);
void fx_preview(fx_style_t s, fx_prev_t *st, uint8_t *g, uint8_t *m);

// Билинейное масштабирование серого кадра (для 160x120 -> 128x96).
void fx_resize_gray(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

// ---------------------------------------------------------------- потоки
// По умолчанию строки обрабатываются по порядку. Прошивка подставляет
// функцию, которая делит строки между двумя ядрами ESP32.
typedef void (*fx_rows_fn)(void *ctx, int y0, int y1);
typedef void (*fx_par_fn)(fx_rows_fn fn, void *ctx, int h);
void fx_set_parallel(fx_par_fn par);
void fx_par_rows(fx_rows_fn fn, void *ctx, int h);

// ---------------------------------------------------------------- PNG
// Индексированный PNG без сжатия (блоки deflate «stored»). Для 1-2 бит
// на пиксель файл всё равно маленький: 800x600 в 1 бите = 60 КБ.
typedef int (*fx_write_fn)(void *user, const void *data, size_t len);
int fx_png_write_indexed(fx_write_fn wr, void *user, const uint8_t *idx, int w, int h,
                         int bpp, const uint8_t (*pal)[3], int pal_n);

// ---------------------------------------------------------------- шрифт
extern const uint8_t fx_font5x7[128][7];

#ifdef __cplusplus
}
#endif
