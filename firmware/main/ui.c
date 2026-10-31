// Интерфейс на однобитном экране: верхняя полоса (стиль, номер кадра),
// кадр 128x96 посередине, нижняя полоса (подсказка, точки стилей, fps).
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled.h"

uint8_t ui_cv[128 * 128];

#define IMG_Y 16

void ui_clear(uint8_t v) { memset(ui_cv, v, sizeof(ui_cv)); }

void ui_px(int x, int y, uint8_t v) {
    if ((unsigned)x < 128 && (unsigned)y < 128) ui_cv[y * 128 + x] = v;
}

void ui_rect(int x, int y, int w, int h, uint8_t v) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) ui_px(xx, yy, v);
}

void ui_frame(int x, int y, int w, int h, uint8_t v) {
    ui_rect(x, y, w, 1, v);
    ui_rect(x, y + h - 1, w, 1, v);
    ui_rect(x, y, 1, h, v);
    ui_rect(x + w - 1, y, 1, h, v);
}

int ui_text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

int ui_text(int x, int y, int scale, const char *s, uint8_t v) {
    const int x0 = x;
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = fx_font5x7[(uint8_t)*s & 0x7F];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r] & (0x10 >> c)) ui_rect(x + c * scale, y + r * scale, scale, scale, v);
    }
    return x - x0;
}

void ui_present(void) { oled_submit(ui_cv); }

static void blit_mask(const uint8_t *m, int y0) { memcpy(ui_cv + y0 * 128, m, FX_PN); }

static void style_dots(fx_style_t s, int y) {
    const int x0 = 128 - FX_COUNT * 4 - 1;
    for (int i = 0; i < FX_COUNT; i++) {
        if (i == (int)s)
            ui_rect(x0 + i * 4, y, 3, 3, 1);
        else
            ui_px(x0 + i * 4 + 1, y + 1, 1);
    }
}

static void top_bar(fx_style_t s, uint32_t shot, bool sd_ok) {
    ui_text(1, 1, 2, fx_info(s)->name, 1);
    char b[12];
    snprintf(b, sizeof(b), "#%04u", (unsigned)(shot % 10000));
    ui_text(127 - ui_text_w(b, 1), 1, 1, b, 1);
    if (sd_ok)
        ui_text(121, 8, 1, "\x05", 1);
    else
        ui_text(97, 8, 1, "NO SD", 1);
}

void ui_live(const uint8_t *mask, const ui_hud_t *h) {
    ui_clear(0);
    top_bar(h->style, h->next_shot, h->sd_ok);
    blit_mask(mask, IMG_Y);
    // Уголки видоискателя поверх кадра (инверсией, видны на любом фоне)
    for (int i = 0; i < 6; i++) {
        const int c[4][2] = {{2 + i, IMG_Y + 2}, {2, IMG_Y + 2 + i}, {125 - i, IMG_Y + 93}, {125, IMG_Y + 93 - i}};
        for (int k = 0; k < 4; k++) ui_cv[c[k][1] * 128 + c[k][0]] ^= 1;
    }
    if (h->banner_left > 0) {
        const char *n = fx_info(h->style)->name;
        const int w = ui_text_w(n, 3);
        const int x = (128 - w) / 2, y = IMG_Y + 48 - 11;
        ui_rect(x - 4, y - 4, w + 8, 21 + 8, 1);
        ui_text(x, y, 3, n, 0);
    }
    // Внизу: выбранный вариант или подсказка стиля (вариант случайный).
    // Пока открыто окно выбора, перед вариантом стоит TAP: касание его меняет.
    const fx_info_t *inf = fx_info(h->style);
    const char *vn = (h->variant >= 0 && h->variant < inf->nvar) ? inf->var[h->variant]
                                                                 : (inf->nvar > 1 ? "RANDOM" : inf->hint);
    char vb[24];
    snprintf(vb, sizeof(vb), h->var_window ? "TAP:%s" : "\x01%s", vn);
    ui_text(1, 119, 1, vb, 1);
    if (h->var_window) ui_rect(1, 127, h->var_window * 60 / 100, 1, 1);  // сколько осталось
    style_dots(h->style, 113);
    char b[16];
    snprintf(b, sizeof(b), "%2dF", h->fps > 99 ? 99 : h->fps);
    ui_text(128 - ui_text_w(b, 1), 119, 1, b, 1);
    ui_present();
}

void ui_flash(void) {
    for (int i = 0; i < 128 * 128; i++) ui_cv[i] ^= 1;
    ui_present();
}

void ui_progress(fx_style_t s, int pct, const char *stage) {
    ui_clear(0);
    ui_text(1, 1, 2, fx_info(s)->name, 1);
    ui_text(4, 40, 2, "RENDER", 1);
    ui_text(4, 58, 1, stage, 1);
    // Сегментная шкала: 20 делений
    const int segs = pct * 20 / 100;
    for (int i = 0; i < 20; i++) {
        if (i < segs)
            ui_rect(4 + i * 6, 72, 5, 10, 1);
        else
            ui_rect(4 + i * 6 + 2, 76, 1, 2, 1);
    }
    char b[16];
    snprintf(b, sizeof(b), "%3d%%", pct);
    ui_text(124 - ui_text_w(b, 2), 88, 2, b, 1);
    // Немного «шума», чтобы экран жил во время ожидания
    for (int i = 0; i < 24; i++) ui_px(esp_random() & 127, 108 + (esp_random() % 18), 1);
    ui_present();
}

void ui_result(const uint8_t *mask, uint32_t shot, const char *style, int ms) {
    ui_clear(0);
    char b[24];
    snprintf(b, sizeof(b), "SAVED #%04u", (unsigned)(shot % 10000));
    ui_text(1, 4, 1, b, 1);
    blit_mask(mask, IMG_Y);
    ui_frame(0, IMG_Y - 1, 128, 98, 1);
    // Внизу: стиль-вариант и сколько занял снимок целиком (съёмка, эффект, запись)
    ui_text(1, 119, 1, style, 1);
    char t[16];
    snprintf(t, sizeof(t), "%d.%dS", ms / 1000, ms / 100 % 10);
    ui_text(128 - ui_text_w(t, 1), 119, 1, t, 1);
    ui_present();
}

void ui_message(const char *l1, const char *l2) {
    ui_clear(0);
    ui_text((128 - ui_text_w(l1, 2)) / 2, 44, 2, l1, 1);
    if (l2) ui_text((128 - ui_text_w(l2, 1)) / 2, 70, 1, l2, 1);
    ui_present();
}

// Заставка: логотип собирается из сдвинутых полос, как сбойный сигнал.
void ui_boot(void) {
    const char *l1 = "DITHR", *l2 = "//CAM";
    for (int f = 0; f < 22; f++) {
        ui_clear(0);
        ui_text(4, 34, 4, l1, 1);
        ui_text(34, 70, 3, l2, 1);
        const int glitch = f < 16 ? 16 - f : 0;
        for (int b = 0; b < 6 && glitch; b++) {
            const int y = esp_random() % 120, hgt = 2 + esp_random() % 6;
            const int dx = (int)(esp_random() % (2 * glitch + 1)) - glitch;
            for (int yy = y; yy < y + hgt && yy < 128; yy++) {
                uint8_t row[128];
                memcpy(row, ui_cv + yy * 128, 128);
                for (int x = 0; x < 128; x++) ui_cv[yy * 128 + x] = row[(x - dx + 128) & 127];
            }
        }
        if (f >= 16) ui_text(4, 110, 1, "OV2640 \x01 SH1107 \x01 SD", 1);
        ui_present();
        vTaskDelay(pdMS_TO_TICKS(45));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
}
