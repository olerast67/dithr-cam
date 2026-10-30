// Кнопки опрашиваются таймером раз в 5 мс. Дребезг гасится счётчиком:
// новое состояние принимается, только если продержалось 4 опроса (20 мс).
// Спуск срабатывает при отпускании короткого нажатия, без ожидания
// двойного клика, поэтому задержки затвора нет.
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
} btn_t;

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
        const bool raw = gpio_get_level(b->pin) == 0;
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
    }
    const esp_timer_create_args_t a = {.callback = tick, .name = "btn"};
    esp_timer_handle_t t;
    esp_timer_create(&a, &t);
    esp_timer_start_periodic(t, TICK_MS * 1000);
}

input_ev_t input_get(void) {
    input_ev_t e = EV_NONE;
    xQueueReceive(s_q, &e, 0);
    return e;
}
