// Декодирование: JPEGDEC (быстрый, умеет уменьшать на лету и считать только
// яркость). Кодирование результата: esp_new_jpeg от Espressif.
#include "codec.h"

#include <string.h>

#include "JPEGDEC.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"

static const char *TAG = "codec";

// Состояние декодера ~20 КБ: держим во внутренней памяти, она быстрее PSRAM.
static JPEGIMAGE s_jpg;

bool jpeg_dims(const uint8_t *b, size_t n, int *w, int *h) {
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (b[i] != 0xFF) {
            i++;
            continue;
        }
        const uint8_t m = b[i + 1];
        if (m == 0xFF) {
            i++;
            continue;
        }
        if (m >= 0xC0 && m <= 0xC2) {
            *h = (b[i + 5] << 8) | b[i + 6];
            *w = (b[i + 7] << 8) | b[i + 8];
            return true;
        }
        if (m == 0xDA) return false;  // начались данные, SOF не встретился
        const size_t len = (size_t)((b[i + 2] << 8) | b[i + 3]);
        i += 2 + len;
    }
    return false;
}

typedef struct {
    uint8_t *dst;
    int dw, dh;
} gray_ctx_t;

static int draw_gray(JPEGDRAW *d) {
    gray_ctx_t *k = d->pUser;
    const uint8_t *src = (const uint8_t *)d->pPixels;
    const int w = d->iWidthUsed ? d->iWidthUsed : d->iWidth;
    for (int r = 0; r < d->iHeight; r++) {
        const int y = d->y + r;
        if (y >= k->dh) break;
        const int n = d->x + w > k->dw ? k->dw - d->x : w;
        if (n > 0) memcpy(k->dst + y * k->dw + d->x, src + r * d->iWidth, (size_t)n);
    }
    return 1;
}

esp_err_t dec_gray_half(const uint8_t *jpg, size_t len, uint8_t *dst, int dw, int dh) {
    gray_ctx_t k = {dst, dw, dh};
    if (!JPEG_openRAM(&s_jpg, (uint8_t *)jpg, (int)len, draw_gray)) return ESP_FAIL;
    s_jpg.pUser = &k;
    JPEG_setPixelType(&s_jpg, EIGHT_BIT_GRAYSCALE);
    const int ok = JPEG_decode(&s_jpg, 0, 0, JPEG_SCALE_HALF | JPEG_LUMA_ONLY);
    JPEG_close(&s_jpg);
    return ok ? ESP_OK : ESP_FAIL;
}

// Куда писать пиксели декодера:
//  - в img целиком или только каналы из mask (бит 0 = R, 1 = G, 2 = B);
//  - в отдельную плоскость plane (один канал ch, размер как у img);
//  - только в строки из полос bands (для сбойного декодирования MOSH).
typedef struct {
    fx_rgb_t *img;
    uint8_t mask;
    uint8_t *plane;
    int ch;
    const int (*bands)[2];
    int nb;
    const uint8_t *tmap;  // TIDE: писать только пиксели, где tmap == tk
    int tk;
    int max_y;  // до какой строки декодер дошёл (сбойный поток может оборваться)
} rgb_ctx_t;

static bool in_bands(const rgb_ctx_t *k, int y) {
    for (int i = 0; i < k->nb; i++)
        if (y >= k->bands[i][0] && y < k->bands[i][1]) return true;
    return false;
}

// RGB8888 от JPEGDEC: в памяти байты R, G, B, A.
static int draw_rgb(JPEGDRAW *d) {
    rgb_ctx_t *k = d->pUser;
    const int W = k->img->w, H = k->img->h;
    const uint8_t *src = (const uint8_t *)d->pPixels;
    const int w = d->iWidthUsed ? d->iWidthUsed : d->iWidth;
    for (int r = 0; r < d->iHeight; r++) {
        const int y = d->y + r;
        if (y >= H) break;
        if (y + 1 > k->max_y) k->max_y = y + 1;
        if (k->bands && !in_bands(k, y)) continue;
        const int n = d->x + w > W ? W - d->x : w;
        const uint8_t *s = src + (size_t)r * d->iWidth * 4;
        if (k->plane) {
            uint8_t *o = k->plane + (size_t)y * W + d->x;
            for (int x = 0; x < n; x++, s += 4) o[x] = s[k->ch];
            continue;
        }
        uint8_t *o = k->img->p + ((size_t)y * W + d->x) * 3;
        if (k->tmap) {
            const uint8_t *t = k->tmap + (size_t)y * W + d->x;
            for (int x = 0; x < n; x++, s += 4, o += 3)
                if (t[x] == k->tk) {
                    o[0] = s[0];
                    o[1] = s[1];
                    o[2] = s[2];
                }
            continue;
        }
        if (k->mask == 7) {
            for (int x = 0; x < n; x++, s += 4, o += 3) {
                o[0] = s[0];
                o[1] = s[1];
                o[2] = s[2];
            }
        } else {
            const int c = k->mask == 1 ? 0 : (k->mask == 2 ? 1 : 2);
            for (int x = 0; x < n; x++, s += 4, o += 3) o[c] = s[c];
        }
    }
    return 1;
}

static esp_err_t dec_run(const uint8_t *jpg, size_t len, rgb_ctx_t *k) {
    if (!JPEG_openRAM(&s_jpg, (uint8_t *)jpg, (int)len, draw_rgb)) return ESP_FAIL;
    s_jpg.pUser = k;
    JPEG_setPixelType(&s_jpg, RGB8888);
    const int jw = JPEG_getWidth(&s_jpg);
    int opt = 0;
    if (jw >= k->img->w * 8) opt = JPEG_SCALE_EIGHTH;
    else if (jw >= k->img->w * 4) opt = JPEG_SCALE_QUARTER;
    else if (jw >= k->img->w * 2) opt = JPEG_SCALE_HALF;
    const int ok = JPEG_decode(&s_jpg, 0, 0, opt);
    if (!ok && !k->bands) ESP_LOGW(TAG, "decode error %d", JPEG_getLastError(&s_jpg));
    JPEG_close(&s_jpg);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t dec_rgb(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint8_t chmask) {
    rgb_ctx_t k = {.img = img, .mask = chmask};
    return dec_run(jpg, len, &k);
}

esp_err_t dec_rgb_tmap(const uint8_t *jpg, size_t len, fx_rgb_t *img, const uint8_t *tmap, int k) {
    rgb_ctx_t c = {.img = img, .mask = 7, .tmap = tmap, .tk = k};
    return dec_run(jpg, len, &c);
}

esp_err_t dec_plane(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint8_t *plane, int ch) {
    rgb_ctx_t k = {.img = img, .plane = plane, .ch = ch};
    return dec_run(jpg, len, &k);
}

// Начало сжатых данных: сразу после заголовка SOS.
static size_t scan_start(const uint8_t *b, size_t n) {
    size_t i = 2;
    while (i + 4 < n) {
        if (b[i] != 0xFF) return 0;
        const uint8_t m = b[i + 1];
        const size_t seg = (size_t)((b[i + 2] << 8) | b[i + 3]);
        if (m == 0xDA) return i + 2 + seg;
        i += 2 + seg;
    }
    return 0;
}

// Таблицы квантования (FF DB): часть коэффициентов умножается. Поток при
// этом остаётся корректным, декодер доходит до конца, а блоки 8x8 в
// полосах становятся грубыми, контрастными и меняют цвет.
static void tamper_dqt(uint8_t *b, size_t n, uint32_t *rng) {
    size_t i = 2;
    while (i + 4 < n && b[i] == 0xFF && b[i + 1] != 0xDA) {
        const size_t seg = (size_t)((b[i + 2] << 8) | b[i + 3]);
        if (b[i + 1] == 0xDB) {
            size_t q = i + 4;
            while (q + 65 <= i + 2 + seg) {
                const int prec16 = b[q] >> 4;
                if (prec16) break;  // 16-битные таблицы не трогаем
                for (int k = 1; k <= 64; k++) {
                    *rng ^= *rng << 13;
                    *rng ^= *rng >> 17;
                    *rng ^= *rng << 5;
                    if ((*rng & 7) < (k < 10 ? 4u : 1u)) {
                        const int v = b[q + k] * (int)(2 + (*rng >> 8) % 6);
                        b[q + k] = (uint8_t)(v > 254 ? 254 : v);  // 0xFF нельзя
                    }
                }
                q += 65;
            }
        }
        i += 2 + seg;
    }
}

// Настоящий датамош: в копии JPEG портятся отдельные биты сжатых данных.
// Декодер сбивается с кода Хаффмана, и дальше по потоку плывут цвет,
// яркость и блоки, пока поток не кончится. Строки берутся только внутри
// полос: остальной кадр остаётся чистым. Маркеры 0xFF не трогаем, иначе
// декодер увидит ложный конец файла.
esp_err_t dec_rgb_glitch(const uint8_t *jpg, size_t len, fx_rgb_t *img, uint32_t seed, uint8_t *tmp, size_t tmp_size) {
    if (len > tmp_size) return ESP_ERR_NO_MEM;
    int bands[8][2];
    const int nb = fx_mosh_bands(seed, img->h, bands, 8);
    const size_t s0 = scan_start(jpg, len);
    if (!s0 || nb <= 0) return ESP_FAIL;
    const size_t data = len - 2 - s0;
    uint32_t rng = seed ^ 0xBADC0DEu;
    int done = 0;  // полосы, до которых декодер уже дошёл
    for (int pass = 0; pass < 3 && done < nb; pass++) {
        memcpy(tmp, jpg, len);
        tamper_dqt(tmp, len, &rng);
        for (int b = done; b < nb; b++) {
            // Порча чуть выше полосы: позиция в потоке примерно пропорциональна строке
            const size_t pos0 = s0 + (size_t)((uint64_t)data * (uint32_t)(bands[b][0] > 8 ? bands[b][0] - 8 : 0) / (uint32_t)img->h);
            const int hits = 1 + (int)(rng % 3);
            for (int k = 0; k < hits; k++) {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                const size_t p = pos0 + rng % 64;
                if (p + 1 >= len - 2 || tmp[p] == 0xFF || tmp[p - 1] == 0xFF) continue;
                const uint8_t v = (uint8_t)(tmp[p] ^ (1u << (rng >> 8 & 7)));
                if (v != 0xFF) tmp[p] = v;
            }
        }
        rgb_ctx_t k = {.img = img, .mask = 7, .bands = (const int(*)[2])bands + done, .nb = nb - done};
        dec_run(tmp, len, &k);
        const int before = done;
        while (done < nb && bands[done][1] <= k.max_y) done++;
        if (done == before) break;  // поток обрывается раньше полосы: оставляем как есть
    }
    return ESP_OK;
}

esp_err_t enc_jpeg(const fx_rgb_t *img, int quality, uint8_t *out, size_t cap, size_t *out_len) {
    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = img->w;
    cfg.height = img->h;
    cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    cfg.subsampling = JPEG_SUBSAMPLE_444;
    cfg.quality = quality;
    // Часть работы (кодирование Хаффмана) уходит в задачу на ядре 0.
    cfg.task_enable = true;
    cfg.hfm_task_priority = 6;
    cfg.hfm_task_core = 0;
    jpeg_enc_handle_t enc = NULL;
    if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) return ESP_FAIL;
    int n = 0;
    const jpeg_error_t e = jpeg_enc_process(enc, img->p, img->w * img->h * 3, out, (int)cap, &n);
    jpeg_enc_close(enc);
    if (e != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "encode error %d", (int)e);
        return ESP_FAIL;
    }
    *out_len = (size_t)n;
    return ESP_OK;
}
