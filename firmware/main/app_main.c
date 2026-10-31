// DITHR//CAM — офлайн-камера с эффектами на ESP32-CAM.
//
// Конвейер превью (ядро 1): кадр JPEG 320x240 -> серый 160x120 (декодер
// считает только яркость и сразу уменьшает вдвое) -> 128x96 -> эффект
// превью -> маска 0/1 -> HUD -> экран. Отправка на экран идёт параллельно
// на ядре 0, пока здесь готовится следующий кадр.
//
// Снимок: камера переключается на 1600x1200, исходный JPEG пишется на карту
// как есть, затем декодируется в 800x600 RGB, обрабатывается эффектом
// (тяжёлые проходы на двух ядрах) и сохраняется в JPEG или PNG.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cam.h"
#include "codec.h"
#include "diag.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fx.h"
#include "input.h"
#include "oled.h"
#include "par.h"
#include "store.h"
#include "ui.h"

static const char *TAG = "dcam";

#define OUT_W (CAP_W / 2)
#define OUT_H (CAP_H / 2)
#define GHOST_GAP_MS 120  // пауза между кадрами GHOST сверх времени декодирования

// ~74 КБ: история кадров GHOST и рабочие буферы превью. Во внутренней памяти
// не помещается, лежит в PSRAM (за кешем это почти так же быстро).
EXT_RAM_BSS_ATTR static fx_prev_t s_prev;
static uint8_t s_g160[160 * 120];
static uint8_t s_g[FX_PN];
static uint8_t s_m[FX_PN];
static fx_rgb_t s_img;
static uint8_t *s_scratch;
static size_t s_scratch_size;
static fx_style_t s_style;
static int s_banner;

static void led_red(bool on) { gpio_set_level(LED_RED_PIN, on ? 0 : 1); }

static void leds_init(void) {
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << LED_RED_PIN) | (1ULL << LED_FLASH_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&c);
    gpio_set_level(LED_FLASH_PIN, 0);
    led_red(false);
}

// ---------------------------------------------------------------- снимок
static fx_style_t s_cap_style;  // стиль снимка (у MIX выбирается случайно)
static int8_t s_var_sel[FX_COUNT];  // выбранный вариант по стилям, -1 = случайный (заполняется при старте)

static void on_progress(int pct, void *user) {
    ui_progress(s_cap_style, pct, (const char *)user);
    vTaskDelay(1);  // даём поработать фоновым задачам и сторожу
}

// Уменьшенная копия результата для показа на экране после снимка.
static void result_mask(const uint8_t *idx, const uint8_t (*pal)[3], int w, int h) {
    for (int y = 0; y < FX_PH; y++)
        for (int x = 0; x < FX_PW; x++) {
            int s = 0;
            const int y0 = y * h / FX_PH, x0 = x * w / FX_PW;
            for (int yy = 0; yy < 6; yy += 2)
                for (int xx = 0; xx < 6; xx += 2) {
                    const size_t i = (size_t)(y0 + yy) * w + x0 + xx;
                    const uint8_t *c = idx ? pal[idx[i]] : s_img.p + i * 3;
                    s += (77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8;
                }
            s_g[y * FX_PW + x] = (uint8_t)(s / 9);
        }
    // Тот же дизеринг, что у превью «RAW»: показывает тон без искажений.
    const uint32_t t = s_prev.t;
    fx_preview(FX_RAW, &s_prev, s_g, s_m);
    s_prev.t = t;
}

static void sd_problem(void) {
    switch (store_state()) {
        case STORE_NOT_FAT: ui_message("NOT FAT32", "RESET TO FORMAT"); break;
        case STORE_READ_ONLY: ui_message("READ ONLY", "SD CARD: REPLACE IT"); break;
        case STORE_NO_MEM: ui_message("SD ERROR", "NO MEMORY"); break;
        default: ui_message("NO SD", "INSERT CARD + RESET"); break;
    }
}

// Ждёт событие кнопки не дольше ms.
static input_ev_t wait_input(int ms) {
    for (int t = 0; t < ms; t += 20) {
        const input_ev_t ev = input_get();
        if (ev != EV_NONE) return ev;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return EV_NONE;
}

// Карта не в FAT32 (чаще всего exFAT на 64 ГБ+). Windows не форматирует в FAT32
// карты больше 32 ГБ, поэтому формат делает сама камера, после двух удержаний.
static void offer_format(void) {
    input_flush();
    ui_message("NOT FAT32", "HOLD = FORMAT CARD");
    input_ev_t ev = wait_input(8000);
    ESP_LOGI(TAG, "format offer: event %d", ev);
    if (ev != EV_NEXT) return;
    input_flush();
    ui_message("ERASE ALL?", "HOLD AGAIN = YES");
    // Удержание повторяет событие: ждём, пока кнопку отпустят, иначе одно
    // долгое нажатие само подтвердило бы стирание.
    while (wait_input(900) != EV_NONE) {
    }
    ev = wait_input(5000);
    ESP_LOGI(TAG, "format confirm: event %d", ev);
    if (ev != EV_NEXT) return;
    ui_message("FORMAT...", "DO NOT POWER OFF");
    const esp_err_t err = store_format();
    char why[32];
    snprintf(why, sizeof(why), "FORMAT %s", esp_err_to_name(err));
    diag_save(why);  // журнал формата напечатается в порт при следующем запуске
    ui_message(err == ESP_OK ? "SD READY" : "FMT FAIL", err == ESP_OK ? "FAT32" : esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1500));
    input_flush();
}

static void capture(void) {
    if (!store_ok()) {
        sd_problem();
        vTaskDelay(pdMS_TO_TICKS(1200));
        return;
    }
    const uint32_t seed = esp_random() | 1;
    // MIX: случайный стиль и вариант на каждый снимок
    s_cap_style = s_style == FX_MIX ? (fx_style_t)(esp_random() % FX_REAL_STYLES) : s_style;
    const fx_info_t *inf = fx_info(s_cap_style);
    const int variant = fx_pick_variant(s_cap_style, s_style == FX_MIX ? -1 : s_var_sel[s_style], seed);

    const int64_t t0 = esp_timer_get_time();
    led_red(true);
    ui_flash();
    const uint32_t shot = store_next_shot();

    const bool tide = s_cap_style == FX_TIDE;
    const int cw = tide ? CAP_W / 2 : CAP_W, chh = tide ? CAP_H / 2 : CAP_H;
    if (tide) fx_tide_map(variant, seed, s_scratch, s_img.w, s_img.h);  // карта времени до съёмки
    dcam_mode_capture(!tide);
    bool ok = true;
    bool saved = true;  // все файлы снимка записались на карту
    char why[32] = "";  // первая причина неудачи: на экран и в журнал
    for (int k = 0; k < inf->frames && ok; k++) {
        camera_fb_t *fb = dcam_get(cw, chh, 8);
        if (!fb) {
            ok = false;
            break;
        }
        if (k == 0) {
            ui_progress(s_cap_style, 2, "SAVE RAW");
            if (store_save(shot, "raw", "jpg", fb->buf, fb->len) != ESP_OK) {
                saved = false;
                snprintf(why, sizeof(why), "RAW %s", store_last_error());
            }
        }
        if (inf->out != FX_OUT_NONE) {
            esp_err_t e;
            if (tide) {
                // TIDE: кадр k ложится только в пиксели своего момента. Первый
                // кадр ложится целиком: если какой-то кадр серии не декодируется,
                // на его месте останется первый, а не прошлый снимок.
                char st[16];
                snprintf(st, sizeof(st), "TIME %d/%d", k + 1, inf->frames);
                ui_progress(s_cap_style, 2 + k * 60 / inf->frames, st);
                e = k == 0 ? dec_rgb(fb->buf, fb->len, &s_img, 7) : dec_rgb_tmap(fb->buf, fb->len, &s_img, s_scratch, k);
            } else if (k == 0) {
                ui_progress(s_cap_style, 4, "DECODE");
                e = dec_rgb(fb->buf, fb->len, &s_img, 7);
                // MOSH/CRPT: поверх чистого кадра полосы из испорченного JPEG
                if (e == ESP_OK && s_cap_style == FX_MOSH && variant == FX_MOSH_CORRUPT)
                    dec_rgb_glitch(fb->buf, fb->len, &s_img, seed, s_scratch, s_scratch_size);
            } else {
                // GHOST: от второго кадра канал G, от третьего канал B
                ui_progress(s_cap_style, 4 + k * 3, "DECODE");
                fx_job_t tmp = {.img = s_img, .scratch = s_scratch};
                e = dec_plane(fb->buf, fb->len, &s_img, k == 1 ? FX_GHOST_G(&tmp) : FX_GHOST_B(&tmp), k);
            }
            if (e != ESP_OK && (!tide || k == 0)) ok = false;  // в серии TIDE битый кадр (кроме первого) пропускаем
        }
        esp_camera_fb_return(fb);
        if (!tide && k + 1 < inf->frames) vTaskDelay(pdMS_TO_TICKS(GHOST_GAP_MS));
    }
    // Превью возвращаем сразу: пока считается эффект, экспозиция успеет
    // устояться под маленький кадр.
    dcam_mode_preview();
    led_red(false);
    const int64_t t1 = esp_timer_get_time();

    if (!ok) {
        ui_message("CAM ERROR", "FRAME LOST");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    char tag[24];
    snprintf(tag, sizeof(tag), "%s-%s", inf->name, inf->var[variant]);
    if (inf->out != FX_OUT_NONE) {
        fx_job_t job = {
            .img = s_img,
            .scratch = s_scratch,
            .seed = seed,
            .shot = shot,
            .variant = variant,
            .progress = on_progress,
            .user = (void *)tag,
        };
        fx_render(s_cap_style, &job);
        const int64_t t2 = esp_timer_get_time();
        if (job.failed) {
            saved = false;  // исходник уже на карте, а полуготовый кадр не пишем
            if (!why[0]) snprintf(why, sizeof(why), "FX NO MEMORY");
        } else if (job.idx) {
            ui_progress(s_cap_style, 100, "WRITE PNG");
            if (store_save_png(shot, tag, job.idx, job.out_w, job.out_h, job.bpp, (const uint8_t(*)[3])job.pal,
                               job.pal_n) != ESP_OK) {
                saved = false;
                if (!why[0]) snprintf(why, sizeof(why), "PNG %s", store_last_error());
            }
            result_mask(job.idx, (const uint8_t(*)[3])job.pal, job.out_w, job.out_h);
        } else {
            ui_progress(s_cap_style, 100, "ENCODE JPEG");
            size_t len = 0;
            // scratch после эффекта свободен: туда кладём готовый JPEG
            if (enc_jpeg(&s_img, 92, s_scratch, s_scratch_size, &len) != ESP_OK) {
                saved = false;
                if (!why[0]) snprintf(why, sizeof(why), "JPEG ENCODE");
            } else if (store_save(shot, tag, "jpg", s_scratch, len) != ESP_OK) {
                saved = false;
                if (!why[0]) snprintf(why, sizeof(why), "JPG %s", store_last_error());
            }
            result_mask(NULL, NULL, s_img.w, s_img.h);
        }
        if (job.failed) result_mask(NULL, NULL, s_img.w, s_img.h);
        ESP_LOGI(TAG, "shot %u %s: grab %lld ms, fx %lld ms, total %lld ms", (unsigned)shot, tag, (t1 - t0) / 1000,
                 (t2 - t1) / 1000, (esp_timer_get_time() - t0) / 1000);
    }
    // Для RAW в s_m остаётся последний кадр превью, его и показываем.
    store_commit_shot(shot);
    if (saved) {
        ui_result(s_m, shot, inf->out == FX_OUT_NONE ? "RAW" : tag, (int)((esp_timer_get_time() - t0) / 1000));
    } else {
        // карта выпала или переполнилась, файл не закодировался или эффекту не
        // хватило памяти: показываем это вместо «SAVED»
        ESP_LOGE(TAG, "save error: %s", why);
        diag_save(why);  // журнал до ошибки напечатается в порт при следующем запуске
        ui_message("SAVE ERROR", why);
    }
    vTaskDelay(pdMS_TO_TICKS(1600));
}

// ---------------------------------------------------------------- цикл
static uint8_t s_m_old[FX_PN];  // последний кадр прежнего стиля
static int64_t s_style_dirty;   // когда стиль сменился и ещё не записан в NVS
static int s_wipe;              // кадров перехода осталось

static void change_style(int d) {
    memcpy(s_m_old, s_m, FX_PN);
    s_wipe = 9;
    s_style = (fx_style_t)(((int)s_style + d + FX_COUNT) % FX_COUNT);
    s_banner = 14;
    s_style_dirty = esp_timer_get_time();  // в NVS запишется, когда листание закончится
}

static void change_variant(void) {
    const int n = fx_info(s_style)->nvar;
    if (n <= 1) return;
    int v = s_var_sel[s_style] + 1;  // -1 (случайный) -> 0 -> ... -> n-1 -> -1
    if (v >= n) v = -1;
    s_var_sel[s_style] = (int8_t)v;
    s_banner = 14;
}

// Глитч-переход: старый кадр рвётся полосами, полосы съезжают и уступают
// место новому стилю.
static void wipe(uint8_t *m) {
    const int t = s_wipe;  // 9 -> 1
    for (int y = 0; y < FX_PH; y += 4) {
        const uint32_t r = esp_random();
        if ((int)(r % 9) >= t) continue;  // эта полоса уже новая
        const int dx = (int)((r >> 8) % 41) - 20;
        for (int yy = y; yy < y + 4; yy++)
            for (int x = 0; x < FX_PW; x++) {
                const uint8_t v = s_m_old[yy * FX_PW + (x - dx + FX_PW) % FX_PW];
                m[yy * FX_PW + x] = (r >> 20) & 1 ? v ^ ((x >> 2) & 1) : v;
            }
    }
    s_wipe--;
}

// Окно выбора варианта: после смены стиля удержанием касания в течение
// VAR_WINDOW_US перебирают варианты, потом касание снова делает снимок.
// Так одной кнопки хватает на всё, а в обычной съёмке затвор срабатывает сразу.
#define VAR_WINDOW_US 1800000
static int64_t s_var_until;

static void camera_loop(void *arg) {
    ui_hud_t hud = {0};
    fx_style_t mix_show = FX_NEON;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const bool window = now < s_var_until && fx_info(s_style)->nvar > 1;
        switch (input_get()) {
            case EV_SHOOT:
                if (window) {
                    change_variant();
                    s_var_until = now + VAR_WINDOW_US;
                } else {
                    capture();
                    input_flush();  // касания во время обработки не должны стать новыми снимками
                    s_var_until = 0;
                }
                break;
            case EV_NEXT:
                change_style(+1);
                s_var_until = now + VAR_WINDOW_US;
                break;
            case EV_PREV:
                change_style(-1);
                s_var_until = now + VAR_WINDOW_US;
                break;
            case EV_VARIANT: change_variant(); break;
            default: break;
        }
        // Стиль пишется во flash через 3 с после последней смены: при листании
        // удержанием это одна запись вместо записи на каждый шаг.
        if (s_style_dirty && now - s_style_dirty > 3000000) {
            store_set_style((int)s_style);
            s_style_dirty = 0;
        }
        camera_fb_t *fb = dcam_get(PREV_W, PREV_H, 4);
        if (!fb) continue;
        const esp_err_t e = dec_gray_half(fb->buf, fb->len, s_g160, 160, 120);
        esp_camera_fb_return(fb);
        if (e != ESP_OK) continue;
        fx_resize_gray(s_g160, 160, 120, s_g, FX_PW, FX_PH);
        // MIX в видоискателе перебирает стили примерно раз в 2 секунды
        if (s_style == FX_MIX && s_prev.t % 40 == 0) mix_show = (fx_style_t)(esp_random() % FX_REAL_STYLES);
        fx_preview(s_style == FX_MIX ? mix_show : s_style, &s_prev, s_g, s_m);
        if (s_wipe > 0) wipe(s_m);

        hud.style = s_style;
        hud.variant = s_var_sel[s_style];
        hud.next_shot = store_peek_shot();
        hud.sd_ok = store_ok();
        hud.fps = oled_fps();
        hud.banner_left = s_banner > 0 ? s_banner-- : 0;
        hud.var_window = window ? (int)((s_var_until - now) * 100 / VAR_WINDOW_US) : 0;
        ui_live(s_m, &hud);
        // Если превью считается дольше кадра камеры, fb_get возвращается сразу и
        // цикл не уступал бы ядро: задача простоя не получала бы времени, и сторож
        // срабатывал бы каждые 20 с.
        vTaskDelay(1);
    }
}

void app_main(void) {
    leds_init();
    // Карта и камера запускаются раньше экрана: их ошибки ещё видны в логе
    // UART, а после запуска экрана GPIO1 становится линией SCL.
    diag_init();
    store_init();
    diag_print_last();
    const esp_err_t cam_err = dcam_init();

    s_img.w = OUT_W;
    s_img.h = OUT_H;
    s_img.p = heap_caps_aligned_alloc(16, (size_t)OUT_W * OUT_H * 3, MALLOC_CAP_SPIRAM);
    s_scratch_size = fx_scratch_size(OUT_W, OUT_H);
    s_scratch = heap_caps_aligned_alloc(16, s_scratch_size, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM free %u, largest %u, internal free %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    par_init();
    fx_set_parallel(par_run);
    fx_preview_init(&s_prev);
    memset(s_var_sel, -1, sizeof(s_var_sel));
    s_style = (fx_style_t)(store_get_style(FX_NEON) % FX_COUNT);
    input_init();

    ESP_LOGI(TAG, "starting OLED, UART log stops here");
    vTaskDelay(pdMS_TO_TICKS(20));
    if (oled_init() != ESP_OK) {
        // Без экрана камера бесполезна: мигаем красным.
        for (;;) {
            led_red(true);
            vTaskDelay(pdMS_TO_TICKS(150));
            led_red(false);
            vTaskDelay(pdMS_TO_TICKS(850));
        }
    }
    ui_boot();
    if (cam_err != ESP_OK || !s_img.p || !s_scratch) {
        ui_message(cam_err != ESP_OK ? "NO CAM" : "NO PSRAM", "CHECK RIBBON / BOARD");
        return;
    }
    if (store_state() == STORE_NOT_FAT) offer_format();
    if (!store_ok()) {
        sd_problem();
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    // Стек с запасом: в глубине снимка работают JPEGDEC и кодировщик JPEG.
    xTaskCreatePinnedToCore(camera_loop, "cam_loop", 12288, NULL, 5, NULL, 1);
}
