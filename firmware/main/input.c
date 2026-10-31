// Кнопки опрашиваются таймером раз в 5 мс. Дребезг гасится счётчиком:
// новое состояние принимается, только если продержалось 4 опроса (20 мс).
// Спуск срабатывает при отпускании короткого нажатия, без ожидания
// двойного клика, поэтому задержки затвора нет.
//
// Подходят два вида кнопок, вид определяется при старте:
//  - механическая на землю: в покое подтяжка даёт 1, нажатие 0;
//  - сенсорный модуль TTP223 (VCC, GND, I/O): он сам держит выход, в покое 0,
//    при касании 1. С включённой подтяжкой вверх в покое читается 0.
// Первые 1,5 с после старта вывод читается с подтяжкой вверх: хоть одна
// единица означает механическую кнопку, иначе это сенсор.
#include "input.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define TICK_MS 5
#define LONG_MS 450
#define REPEAT_MS 650

typedef struct {
    int pin;
    input_ev_t on_click, on_long;
    bool down;     // принятое состояние
    uint8_t cnt;   // сколько опросов подряд сырое состояние отличается
    int32_t held_ms;  // со знаком: после срабатывания уходит в минус до повтора
    bool long_fired;
    bool active_high;  // сенсорный модуль: касание = 1
    int16_t detect_ms;  // > 0: ещё определяем вид кнопки, нажатия не считаются
} btn_t;

#define DETECT_MS 1500

static btn_t s_btn[2] = {
    {.pin = BTN_SHUTTER_PIN, .on_click = EV_SHOOT, .on_long = EV_NEXT},
    {.pin = BTN_MODE_PIN, .on_click = EV_VARIANT, .on_long = EV_PREV},
};
static QueueHandle_t s_q;

static void push(input_ev_t e) { xQueueSend(s_q, &e, 0); }

static void tick(void *arg) {
    for (int i = 0; i < 2; i++) {
        btn_t *b = &s_btn[i];
        if (b->pin < 0) continue;
        if (b->detect_ms > 0) {
            // Механическая кнопка с подтяжкой в покое даёт 1. Выход TTP223 в покое 0.
            // Если механическую держали при включении, 1 появится, как только её отпустят.
            if (gpio_get_level(b->pin) == 1) {
                b->detect_ms = 0;
                b->active_high = false;
            } else if ((b->detect_ms -= TICK_MS) <= 0) {
                b->active_high = true;
                gpio_set_pull_mode(b->pin, GPIO_PULLDOWN_ONLY);  // выход модуля двухтактный
            }
            continue;
        }
        const bool raw = gpio_get_level(b->pin) == (b->active_high ? 1 : 0);
        if (raw != b->down) {
            if (++b->cnt >= 4) {
                b->down = raw;
                b->cnt = 0;
                if (raw) {
                    b->held_ms = 0;
                    b->long_fired = false;
                } else if (!b->long_fired) {
                    push(b->on_click);
                }
            }
        } else {
            b->cnt = 0;
        }
        if (b->down) {
            b->held_ms += TICK_MS;
            if (b->held_ms >= LONG_MS) {
                push(b->on_long);
                b->long_fired = true;
                b->held_ms = LONG_MS - REPEAT_MS;  // следующий повтор через REPEAT_MS
            }
        }
    }
}

void input_init(void) {
    s_q = xQueueCreate(8, sizeof(input_ev_t));
    for (int i = 0; i < 2; i++) {
        if (s_btn[i].pin < 0) continue;
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << s_btn[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&c);
        s_btn[i].active_high = BTN_KIND == 2;
        s_btn[i].detect_ms = BTN_KIND == 0 ? DETECT_MS : 0;
        if (BTN_KIND == 2) gpio_set_pull_mode(s_btn[i].pin, GPIO_PULLDOWN_ONLY);
    }
    const esp_timer_create_args_t a = {.callback = tick, .name = "btn"};
    esp_timer_handle_t t;
    esp_timer_create(&a, &t);
    esp_timer_start_periodic(t, TICK_MS * 1000);
}

bool input_is_touch(void) { return s_btn[0].active_high; }

void input_flush(void) { xQueueReset(s_q); }

input_ev_t input_get(void) {
    input_ev_t e = EV_NONE;
    xQueueReceive(s_q, &e, 0);
    return e;
}
