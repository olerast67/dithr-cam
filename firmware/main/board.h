// Распиновка ESP32-CAM (AI-Thinker и совместимые, в том числе Estardyn).
// Всё, что можно менять под свою сборку, собрано здесь.
#pragma once

// ---- Камера OV2640 (разведена на плате, не трогать)
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
// Если кадр вверх ногами или зеркальный, поменяйте 0 на 1.
#define CAM_VFLIP 0
#define CAM_HMIRROR 0

// ---- microSD на плате, режим 1 бит: CLK 14, CMD 15, D0 2.
// Освобождаются GPIO 4, 12, 13.

// ---- OLED SH1107 128x128, I2C.
// SDA на GPIO13, SCL на GPIO1 (это TX UART). После старта экрана лог в
// порт пропадает, прошивка через USB при этом работает как обычно.
// SCL нельзя вешать на GPIO12: подтяжка модуля при старте выберет 1.8 В
// для flash, и плата не загрузится.
#define OLED_PIN_SDA 13
#define OLED_PIN_SCL 1
#define OLED_I2C_ADDR 0x3C
// По спецификации SH1107 400 кГц. Многие модули тянут 800-1000 кГц:
// превью станет плавнее. Если картинка рвётся, верните 400000.
#define OLED_I2C_HZ 400000
// Поворот картинки на экране: 0, 90, 180 или 270 градусов.
#define OLED_ROTATE 0
// Сдвиг столбцов: у части модулей 128x128 картинка уезжает, тогда 96 или 32.
#define OLED_COL_OFFSET 0

// ---- Кнопки (замыкают на GND, подтяжка внутренняя)
// GPIO12 безопасен: при загрузке он должен быть низким, кнопка на землю
// это никогда не нарушит.
#define BTN_SHUTTER_PIN 12
// Вторая кнопка необязательна. GPIO3 (RX) годится, только когда плата
// питается без программатора ESP32-CAM-MB: на MB этот пин держит CH340.
#define BTN_MODE_PIN -1

// ---- Светодиоды
#define LED_RED_PIN 33   // маленький красный сзади, горит при низком уровне
#define LED_FLASH_PIN 4  // яркая вспышка, держим выключенной
