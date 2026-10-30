#pragma once
#include "esp_camera.h"
#include "esp_err.h"

// Снимок: UXGA 1600x1200, декодируется с уменьшением вдвое до 800x600.
#define CAP_W 1600
#define CAP_H 1200
#define PREV_W 320
#define PREV_H 240

esp_err_t dcam_init(void);
void dcam_mode_preview(void);
// big = 1: 1600x1200 (обычный снимок), 0: 800x600 (серии TIDE, кадры чаще).
void dcam_mode_capture(int big);
// Кадр с нужными размерами по заголовку JPEG. Устаревшие кадры после смены
// разрешения отбрасываются: у них в fb->width уже новое значение, а внутри
// старая картинка.
camera_fb_t *dcam_get(int w, int h, int max_tries);
