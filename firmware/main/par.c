// Верхнюю половину строк считает задача-помощник на ядре 0, нижнюю
// вызывающая задача на ядре 1. Строки независимы, поэтому блокировки не
// нужны: только «старт» уведомлением и «готово» семафором.
#include "par.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static TaskHandle_t s_worker;
static SemaphoreHandle_t s_done;
static fx_rows_fn s_fn;
static void *s_ctx;
static int s_y1;

static void worker(void *arg) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_fn(s_ctx, 0, s_y1);
        xSemaphoreGive(s_done);
    }
}

void par_init(void) {
    s_done = xSemaphoreCreateBinary();
    // Приоритет выше вывода на экран: пока идёт обработка, экран подождёт.
    xTaskCreatePinnedToCore(worker, "par", 4096, NULL, 6, &s_worker, 0);
}

void par_run(fx_rows_fn fn, void *ctx, int h) {
    const int mid = h / 2;
    s_fn = fn;
    s_ctx = ctx;
    s_y1 = mid;
    xTaskNotifyGive(s_worker);
    fn(ctx, mid, h);
    xSemaphoreTake(s_done, portMAX_DELAY);
}
