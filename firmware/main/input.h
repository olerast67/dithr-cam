#pragma once

typedef enum {
    EV_NONE,
    EV_SHOOT,  // короткое нажатие спуска
    EV_NEXT,   // удержание спуска (с автоповтором)
    EV_PREV,     // удержание второй кнопки
    EV_VARIANT,  // нажатие второй кнопки: следующий вариант стиля
} input_ev_t;

void input_init(void);
input_ev_t input_get(void);  // не ждёт: EV_NONE, если событий нет
