#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "fx.h"

// Размеры из маркера SOF внутри JPEG.
bool jpeg_dims(const uint8_t *b, size_t n, int *w, int *h);

// Превью: JPEG -> серый в половину размера (только яркость, цвет не считается).
esp_err_t dec_gray_half(const uint8_t *jpg, size_t len, uint8_t *dst, int dw, int dh);

// Снимок: JPEG -> RGB888 в img с уменьшением до размера img. chmask: какие
// каналы писать (бит 0 = R, 1 = G, 2 = B); для GHOST каждый кадр пишет свой.
esp_err_t dec_rgb(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint8_t chmask);

// Один канал ch (0..2) кадра в отдельную плоскость размером img->w x img->h.
esp_err_t dec_plane(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint8_t *plane, int ch);

// TIDE: кадр k вписывается только в пиксели, где карта времени tmap == k.
esp_err_t dec_rgb_tmap(const uint8_t *jpg, size_t len, fx_rgb_t *img, const uint8_t *tmap, int k);

// MOSH/CRPT: поверх уже декодированного img вписать полосы из испорченной
// копии JPEG (копия кладётся в tmp).
esp_err_t dec_rgb_glitch(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint32_t seed, uint8_t *tmp, size_t tmp_size);

// RGB888 -> JPEG 4:4:4 (без потери цвета на неоновых краях).
esp_err_t enc_jpeg(const fx_rgb_t *img, int quality, uint8_t *out, size_t cap, size_t *out_len);
