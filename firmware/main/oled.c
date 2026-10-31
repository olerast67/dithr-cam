// SH1107 128x128 по I2C. Память контроллера: 16 страниц по 128 столбцов,
// байт = 8 пикселей по вертикали. Адрес сам на следующую страницу не
// переходит, поэтому кадр уходит 16 транзакциями: заголовок страницы и
// 128 байт данных. Заголовок и данные передаются двумя буферами одной
// транзакции, без копирования.
#include "oled.h"

#include <string.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "oled";

#ifndef OLED_ROTATE
#define OLED_ROTATE 0  // 0, 90, 180, 270: поворот картинки программно
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static uint8_t s_pending[16][128];
static uint8_t s_front[16][128];
static volatile bool s_has_pending;
static volatile int s_fps;

static esp_err_t cmds(const uint8_t *c, size_t n) {
    uint8_t buf[32];
    buf[0] = 0x00;  // дальше поток команд
    memcpy(buf + 1, c, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 100);
}

void oled_contrast(uint8_t c) {
    const uint8_t b[2] = {0x81, c};
    cmds(b, 2);
}

static void flush(void) {
    for (int p = 0; p < 16; p++) {
        const int col = OLED_COL_OFFSET & 0x7F;
        uint8_t hdr[7] = {0x80, (uint8_t)(0xB0 | p), 0x80, (uint8_t)(0x10 | (col >> 4)), 0x80, (uint8_t)(col & 0x0F),
                          0x40};
        i2c_master_transmit_multi_buffer_info_t parts[2] = {
            {.write_buffer = hdr, .buffer_size = sizeof(hdr)},
            {.write_buffer = s_front[p], .buffer_size = 128},
        };
        if (i2c_master_multi_buffer_transmit(s_dev, parts, 2, 100) != ESP_OK) return;
    }
}

static void oled_task(void *arg) {
    int64_t t0 = esp_timer_get_time();
    int frames = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(s_front, s_pending, sizeof(s_front));
        s_has_pending = false;
        xSemaphoreGive(s_lock);
        flush();
        frames++;
        const int64_t now = esp_timer_get_time();
        if (now - t0 >= 1000000) {
            s_fps = (int)(frames * 1000000LL / (now - t0));
            frames = 0;
            t0 = now;
        }
    }
}

int oled_fps(void) { return s_fps; }

void oled_submit(const uint8_t *cv) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_pending, 0, sizeof(s_pending));
    for (int y = 0; y < OLED_H; y++) {
        const uint8_t *row = cv + y * OLED_W;
        for (int x = 0; x < OLED_W; x++) {
            if (!row[x]) continue;
            int px, py;
#if OLED_ROTATE == 90
            px = OLED_W - 1 - y;
            py = x;
#elif OLED_ROTATE == 180
            px = OLED_W - 1 - x;
            py = OLED_H - 1 - y;
#elif OLED_ROTATE == 270
            px = y;
            py = OLED_H - 1 - x;
#else
            px = x;
            py = y;
#endif
            s_pending[py >> 3][px] |= (uint8_t)(1u << (py & 7));
        }
    }
    s_has_pending = true;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

esp_err_t oled_init(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,  // порт 1 занят камерой (SCCB)
        .sda_io_num = OLED_PIN_SDA,
        .scl_io_num = OLED_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bc, &s_bus);
    if (err != ESP_OK) return err;

    uint16_t addr = OLED_I2C_ADDR;
    if (i2c_master_probe(s_bus, addr, 50) != ESP_OK) {
        addr = OLED_I2C_ADDR ^ 1;  // 0x3C <-> 0x3D
        if (i2c_master_probe(s_bus, addr, 50) != ESP_OK) {
            ESP_LOGE(TAG, "SH1107 not found on I2C");
            return ESP_ERR_NOT_FOUND;
        }
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = OLED_I2C_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dc, &s_dev);
    if (err != ESP_OK) return err;

    // Последовательность из U8g2 (sh1107 128x128) с уточнениями Adafruit.
    static const uint8_t init[] = {
        0xAE,        // экран выкл
        0xDC, 0x00,  // начальная строка
        0x81, 0x80,  // контраст
        0x20,        // страничная адресация
        0xA0,        // сегменты без зеркала
        0xC0,        // строки сверху вниз
        0xA8, 0x7F,  // мультиплекс 128
        0xD3, 0x00,  // смещение экрана
        0xD5, 0xF0,  // генератор на максимум, без делителя: меньше полос на видео с телефона
        0xD9, 0x22,  // предзаряд
        0xDB, 0x35,  // VCOMH
        0xAD, 0x8A,  // встроенный DC-DC
        0xA4,        // выводить память
        0xA6,        // без инверсии
    };
    err = cmds(init, sizeof(init));
    if (err != ESP_OK) return err;

    s_lock = xSemaphoreCreateMutex();
    memset(s_front, 0, sizeof(s_front));
    flush();
    static const uint8_t on = 0xAF;
    cmds(&on, 1);
    // Ядро 0: там же служебная задача камеры, ядро 1 свободно под конвейер.
    xTaskCreatePinnedToCore(oled_task, "oled", 3072, NULL, 4, &s_task, 0);
    ESP_LOGI(TAG, "SH1107 at 0x%02X, %d Hz", addr, OLED_I2C_HZ);
    return ESP_OK;
}
