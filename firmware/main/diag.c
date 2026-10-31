#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define RING 3072

static char s_ring[RING];
static size_t s_pos;   // куда писать следующий байт
static bool s_full;    // кольцо уже переполнялось
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev;

static int hook(const char *fmt, va_list args) {
    char line[200];
    va_list copy;
    va_copy(copy, args);
    const int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    const size_t len = n < 0 ? 0 : (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < len; i++) {
        s_ring[s_pos++] = line[i];
        if (s_pos == RING) {
            s_pos = 0;
            s_full = true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return s_prev ? s_prev(fmt, args) : vprintf(fmt, args);
}

void diag_init(void) { s_prev = esp_log_set_vprintf(hook); }

void diag_save(const char *why) {
    static char out[RING + 64];
    size_t o = (size_t)snprintf(out, 64, "SAVE ERROR: %s\n", why);
    portENTER_CRITICAL(&s_lock);
    if (s_full) {
        memcpy(out + o, s_ring + s_pos, RING - s_pos);
        o += RING - s_pos;
    }
    memcpy(out + o, s_ring, s_pos);
    o += s_pos;
    portEXIT_CRITICAL(&s_lock);
    nvs_handle_t h;
    if (nvs_open("dcam", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "lasterr", out, o);
        nvs_commit(h);
        nvs_close(h);
    }
}

void diag_print_last(void) {
    nvs_handle_t h;
    if (nvs_open("dcam", NVS_READWRITE, &h) != ESP_OK) return;
    size_t n = 0;
    if (nvs_get_blob(h, "lasterr", NULL, &n) == ESP_OK && n > 0) {
        static char buf[RING + 64 + 1];
        if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
        if (nvs_get_blob(h, "lasterr", buf, &n) == ESP_OK) {
            buf[n] = 0;
            printf("\n===== последняя ошибка снимка =====\n%s\n===== конец =====\n", buf);
        }
        nvs_erase_key(h, "lasterr");
        nvs_commit(h);
    }
    nvs_close(h);
}
