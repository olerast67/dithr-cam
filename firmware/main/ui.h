#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "fx.h"

// Холст 128x128, байт на пиксель (0/1). Всё рисуется сюда, потом ui_present().
extern uint8_t ui_cv[128 * 128];

void ui_clear(uint8_t v);
void ui_px(int x, int y, uint8_t v);
void ui_rect(int x, int y, int w, int h, uint8_t v);
void ui_frame(int x, int y, int w, int h, uint8_t v);
int ui_text(int x, int y, int scale, const char *s, uint8_t v);
int ui_text_w(const char *s, int scale);
void ui_present(void);

typedef struct {
    fx_style_t style;
    int variant;  // выбранный вариант, -1 = случайный
    uint32_t next_shot;
    bool sd_ok;
    int fps;
    int banner_left;  // > 0: крупное имя стиля поверх кадра, ещё N кадров
    int var_window;   // > 0: открыто окно выбора варианта, осталось столько процентов
} ui_hud_t;

void ui_boot(void);
void ui_live(const uint8_t *mask, const ui_hud_t *h);
void ui_flash(void);
void ui_progress(fx_style_t s, int pct, const char *stage);
void ui_result(const uint8_t *mask, uint32_t shot, const char *style, int ms);
void ui_message(const char *l1, const char *l2);
