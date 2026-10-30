// Симулятор экрана камеры: тот же ui.c и тот же fx, что в прошивке, но
// кадры камеры читаются из файла, а экран пишется в файл кадров.
//
//   oled_sim frames.bin cap.ppm out.bin
//   frames.bin: подряд кадры 160x120 серые (как после декодера превью)
//   cap.ppm:    800x600 RGB, кадр «снимка»
//   out.bin:    [uint16 мс][128*128 байт 0/1] на каждый кадр экрана
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/task.h"
#include "fx.h"
#include "oled.h"
#include "ui.h"

static FILE *s_out;
static uint8_t s_last[128 * 128];
static int s_have;
static uint32_t s_dur;
static uint32_t s_rng = 0xC0FFEE;

static void emit(void) {
    if (!s_have) return;
    uint16_t d = (uint16_t)(s_dur ? s_dur : 50);
    fwrite(&d, 2, 1, s_out);
    fwrite(s_last, 1, sizeof(s_last), s_out);
    s_have = 0;
    s_dur = 0;
}

void oled_submit(const uint8_t *cv) {
    emit();
    memcpy(s_last, cv, sizeof(s_last));
    s_have = 1;
}
int oled_fps(void) { return 20; }
void vTaskDelay(TickType_t ms) { s_dur += ms; }
uint32_t esp_random(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static FILE *s_frames;
static fx_prev_t s_prev;
static uint8_t s_g160[160 * 120], s_g[FX_PN], s_m[FX_PN];
static fx_style_t s_style;
static int s_banner;

static void live(int n) {
    ui_hud_t hud = {0};
    for (int i = 0; i < n; i++) {
        if (fread(s_g160, 1, sizeof(s_g160), s_frames) != sizeof(s_g160)) return;
        fx_resize_gray(s_g160, 160, 120, s_g, FX_PW, FX_PH);
        fx_preview(s_style, &s_prev, s_g, s_m);
        hud.style = s_style;
        hud.next_shot = 42;
        hud.sd_ok = true;
        hud.fps = 20;
        hud.banner_left = s_banner > 0 ? s_banner-- : 0;
        ui_live(s_m, &hud);
        vTaskDelay(50);
    }
}

static void on_progress(int pct, void *user) {
    ui_progress(s_style, pct, (const char *)user);
    vTaskDelay(280);  // примерно как на ESP32
}

int main(int argc, char **argv) {
    s_frames = fopen(argv[1], "rb");
    s_out = fopen(argv[3], "wb");
    fx_preview_init(&s_prev);

    ui_boot();
    // Стили, которые лучше всего смотрятся на однобитном экране
    static const fx_style_t order[] = {FX_LOOP, FX_TIDE, FX_PULSE, FX_HOLO, FX_TOPO, FX_SHARD, FX_NEON, FX_DMG};
    const int n = (int)(sizeof(order) / sizeof(order[0]));
    for (int k = 0; k < n; k++) {
        s_style = order[k];
        s_banner = 14;
        live(k < 2 ? 70 : 45);  // LOOP и TIDE подольше: они двигаются сами
    }
    s_style = FX_PULSE;
    s_banner = 14;
    live(30);

    // Снимок
    for (int i = 0; i < 128 * 128; i++) ui_cv[i] ^= 1;
    ui_present();
    vTaskDelay(80);
    ui_progress(s_style, 2, "SAVE RAW");
    vTaskDelay(150);
    ui_progress(s_style, 4, "DECODE");
    vTaskDelay(400);
    FILE *f = fopen(argv[2], "rb");
    int w, h, mx;
    fscanf(f, "P6 %d %d %d", &w, &h, &mx);
    fgetc(f);
    fx_rgb_t img = {malloc((size_t)w * h * 3), w, h};
    fread(img.p, 1, (size_t)w * h * 3, f);
    fclose(f);
    fx_job_t job = {.img = img, .scratch = malloc(fx_scratch_size(w, h)), .seed = 1234567, .shot = 42, .variant = 1,
                    .progress = on_progress, .user = (void *)"FX"};
    fx_render(s_style, &job);
    ui_progress(s_style, 100, "ENCODE JPEG");
    vTaskDelay(500);
    // Превью результата: как result_mask() в прошивке
    for (int y = 0; y < FX_PH; y++)
        for (int x = 0; x < FX_PW; x++) {
            int s = 0;
            for (int yy = 0; yy < 6; yy += 2)
                for (int xx = 0; xx < 6; xx += 2) {
                    const uint8_t *c = img.p + ((size_t)(y * h / FX_PH + yy) * w + x * w / FX_PW + xx) * 3;
                    s += (77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8;
                }
            s_g[y * FX_PW + x] = (uint8_t)(s / 9);
        }
    fx_preview(FX_RAW, &s_prev, s_g, s_m);
    ui_result(s_m, 42, "PULSE-CHROMA", 2400);
    vTaskDelay(1800);
    // результат отдельно в цвете, для подписи к анимации
    f = fopen("sim_shot.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(img.p, 1, (size_t)w * h * 3, f);
    fclose(f);
    live(30);
    emit();
    fclose(s_out);
    return 0;
}
