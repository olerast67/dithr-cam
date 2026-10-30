#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STORE_DIR "/sdcard/DITHR"

esp_err_t store_init(void);
bool store_ok(void);
uint32_t store_next_shot(void);  // ищет свободный номер на карте (медленно, только для снимка)
uint32_t store_peek_shot(void);  // номер без обращения к карте, для HUD
void store_commit_shot(uint32_t shot);

// Файлы снимка: 0042_raw.jpg (исходник камеры) и 0042_NEON.jpg / .png.
esp_err_t store_save(uint32_t shot, const char *tag, const char *ext, const uint8_t *buf, size_t len);
esp_err_t store_save_png(uint32_t shot, const char *tag, const uint8_t *idx, int w, int h, int bpp,
                         const uint8_t (*pal)[3], int pal_n);

// Сохранённый стиль между включениями.
int store_get_style(int def);
void store_set_style(int style);
