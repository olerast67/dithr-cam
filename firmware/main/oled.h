#pragma once
#include <stdint.h>

#include "esp_err.h"

#define OLED_W 128
#define OLED_H 128

// Запуск шины I2C, контроллера SH1107 и задачи вывода на ядре 0.
esp_err_t oled_init(void);

// Отдать кадр: холст 128x128, байт на пиксель (0/1). Кадр упаковывается
// и копируется, функция не ждёт окончания передачи. Если экран не успел
// показать предыдущий кадр, тот просто заменяется свежим.
void oled_submit(const uint8_t *canvas);

// Сколько кадров в секунду реально уходит на экран.
int oled_fps(void);

void oled_contrast(uint8_t c);
