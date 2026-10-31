// OV2640 в режиме JPEG. Буферы кадров выделяются при запуске под самый
// большой размер (UXGA), поэтому дальше можно переключаться между превью
// QVGA и снимком UXGA без перезапуска драйвера и без сброса экспозиции.
#include "cam.h"

#include "board.h"
#include "codec.h"
#include "esp_log.h"

static const char *TAG = "cam";

#ifndef CAM_VFLIP
#define CAM_VFLIP 0
#endif
#ifndef CAM_HMIRROR
#define CAM_HMIRROR 0
#endif

#define Q_PREVIEW 12  // меньше число = лучше качество и больше файл
#define Q_CAPTURE 12

esp_err_t dcam_init(void) {
    camera_config_t c = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        // Старт сразу в малом режиме: датчик включается с меньшим током, чем в
        // 1600x1200 (на слабом USB при включении камеры срабатывала защита от
        // просадки питания). Буферы всё равно размером jpeg_buffer_size, поэтому
        // переключение на 1600x1200 для снимка работает.
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = Q_CAPTURE,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = 1,
        // Запас к стандартным w*h/5: на мелких деталях (листва) кадр UXGA
        // при качестве 10 бывает крупнее 384 КБ, и тогда он обрезается.
        .jpeg_buffer_size = 448 * 1024,
    };
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init: %s", esp_err_to_name(err));
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_bpc(s, 1);       // чинить «битые» чёрные пиксели
    s->set_wpc(s, 1);       // и белые
    s->set_lenc(s, 1);      // коррекция затемнения к краям объектива
    s->set_raw_gma(s, 1);
    s->set_dcw(s, 1);       // честное уменьшение в сенсоре для малых размеров
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, GAINCEILING_4X);  // меньше шума в темноте
    // На части плат вместо OV2640 стоит OV3660 (3 Мп). Он отдаёт кадр вверх
    // ногами и пересвечивает цвет, поэтому для него переворот включён наоборот,
    // яркость чуть выше, насыщенность ниже (как в примере Espressif CameraWebServer).
    const bool ov3660 = s->id.PID == OV3660_PID;
    if (ov3660) {
        // На 20 МГц OV3660 гонит JPEG быстрее, чем ESP32 успевает забирать по DMA:
        // байты теряются, и в кадре появляются однотонные блоки и сдвиг цвета.
        s->set_xclk(s, LEDC_TIMER_0, 10);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
    }
    ESP_LOGI(TAG, "sensor PID 0x%04x%s", s->id.PID, ov3660 ? " (OV3660)" : "");
    s->set_vflip(s, ov3660 ? !CAM_VFLIP : CAM_VFLIP);
    s->set_hmirror(s, CAM_HMIRROR);
    dcam_mode_preview();
    return ESP_OK;
}

void dcam_mode_preview(void) {
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, Q_PREVIEW);
}

void dcam_mode_capture(int big) {
    sensor_t *s = esp_camera_sensor_get();
    s->set_quality(s, Q_CAPTURE);
    s->set_framesize(s, big ? FRAMESIZE_UXGA : FRAMESIZE_SVGA);
}

camera_fb_t *dcam_get(int w, int h, int max_tries) {
    for (int i = 0; i < max_tries; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;
        int jw, jh;
        if (jpeg_dims(fb->buf, fb->len, &jw, &jh) && jw == w && jh == h) return fb;
        esp_camera_fb_return(fb);
    }
    return NULL;
}
