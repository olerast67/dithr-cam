// Карта памяти в режиме 1 бит (GPIO 14/15/2) и запись файлов.
// Данные из PSRAM карта по DMA забрать не может, поэтому пишем кусками по
// 16 КБ через внутренний буфер: так быстрее, чем побайтовая stdio.
#include "store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "fx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

static const char *TAG = "store";
#define CHUNK (16 * 1024)

static bool s_ok;
static sdmmc_card_t *s_card;
static uint8_t *s_bounce;
static uint32_t s_shot = 1;

static uint32_t nvs_get(const char *key, uint32_t def) {
    nvs_handle_t h;
    uint32_t v = def;
    if (nvs_open("dcam", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, key, &v);
        nvs_close(h);
    }
    return v;
}

static void nvs_put(const char *key, uint32_t v) {
    nvs_handle_t h;
    if (nvs_open("dcam", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static esp_err_t mount(int khz) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = khz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 32 * 1024,
    };
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &s_card);
}

esp_err_t store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    s_shot = nvs_get("shot", 1);
    s_bounce = heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    // 40 МГц официально работает в 1-битном режиме; если карта капризничает,
    // откатываемся на 20 МГц.
    err = mount(SDMMC_FREQ_HIGHSPEED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD at 40 MHz: %s, retry 20 MHz", esp_err_to_name(err));
        err = mount(SDMMC_FREQ_DEFAULT);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no SD card: %s", esp_err_to_name(err));
        return err;
    }
    sdmmc_card_print_info(stdout, s_card);
    mkdir(STORE_DIR, 0777);
    // Номер в NVS мог сброситься (стирание flash, новая версия NVS). Чтобы не
    // затереть старые снимки, продолжаем после самого большого номера на карте.
    DIR *d = opendir(STORE_DIR);
    if (d) {
        struct dirent *e;
        uint32_t top = 0;
        while ((e = readdir(d)) != NULL) {
            const char *n = e->d_name;
            if (n[0] >= '0' && n[0] <= '9' && n[1] >= '0' && n[1] <= '9' && n[2] >= '0' && n[2] <= '9' &&
                n[3] >= '0' && n[3] <= '9' && n[4] == '_') {
                const uint32_t v = (uint32_t)((n[0] - '0') * 1000 + (n[1] - '0') * 100 + (n[2] - '0') * 10 + (n[3] - '0'));
                if (v > top) top = v;
            }
        }
        closedir(d);
        if (top + 1 > s_shot) s_shot = top + 1;
    }
    s_ok = s_bounce != NULL;
    return s_ok ? ESP_OK : ESP_ERR_NO_MEM;
}

bool store_ok(void) { return s_ok; }

static void path_for(char *p, size_t n, uint32_t shot, const char *tag, const char *ext) {
    snprintf(p, n, STORE_DIR "/%04u_%s.%s", (unsigned)shot, tag, ext);
}

uint32_t store_next_shot(void) {
    // Если карту меняли, номер мог уже существовать: ищем свободный.
    char p[64];
    struct stat st;
    for (int i = 0; i < 10000; i++) {
        path_for(p, sizeof(p), s_shot, "raw", "jpg");
        if (stat(p, &st) != 0) break;
        s_shot++;
    }
    return s_shot;
}

uint32_t store_peek_shot(void) { return s_shot; }

void store_commit_shot(uint32_t shot) {
    s_shot = shot + 1;
    nvs_put("shot", s_shot);
}

int store_get_style(int def) { return (int)nvs_get("style", (uint32_t)def); }
void store_set_style(int style) { nvs_put("style", (uint32_t)style); }

typedef struct {
    int fd;
    size_t fill;
    int err;
} wr_t;

static int wr_flush(wr_t *w) {
    if (w->fill && !w->err) {
        if (write(w->fd, s_bounce, w->fill) != (ssize_t)w->fill) w->err = errno ? errno : EIO;
    }
    w->fill = 0;
    return w->err;
}

static int wr_put(void *user, const void *data, size_t len) {
    wr_t *w = user;
    const uint8_t *d = data;
    while (len && !w->err) {
        const size_t n = len < CHUNK - w->fill ? len : CHUNK - w->fill;
        memcpy(s_bounce + w->fill, d, n);
        w->fill += n;
        d += n;
        len -= n;
        if (w->fill == CHUNK) wr_flush(w);
    }
    return w->err ? -1 : 0;
}

static esp_err_t open_wr(wr_t *w, uint32_t shot, const char *tag, const char *ext) {
    char p[64];
    path_for(p, sizeof(p), shot, tag, ext);
    w->fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    w->fill = 0;
    w->err = 0;
    if (w->fd < 0) {
        ESP_LOGE(TAG, "open %s: %d", p, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t close_wr(wr_t *w) {
    wr_flush(w);
    // FAT дописывает последний сектор и запись каталога только в close():
    // если карта отвалилась здесь, файл обрезан, и это ошибка.
    if (close(w->fd) != 0 && !w->err) w->err = errno ? errno : EIO;
    return w->err ? ESP_FAIL : ESP_OK;
}

esp_err_t store_save(uint32_t shot, const char *tag, const char *ext, const uint8_t *buf, size_t len) {
    if (!s_ok) return ESP_ERR_INVALID_STATE;
    wr_t w;
    if (open_wr(&w, shot, tag, ext) != ESP_OK) return ESP_FAIL;
    wr_put(&w, buf, len);
    return close_wr(&w);
}

esp_err_t store_save_png(uint32_t shot, const char *tag, const uint8_t *idx, int w, int h, int bpp,
                         const uint8_t (*pal)[3], int pal_n) {
    if (!s_ok) return ESP_ERR_INVALID_STATE;
    wr_t wr;
    if (open_wr(&wr, shot, tag, "png") != ESP_OK) return ESP_FAIL;
    if (fx_png_write_indexed(wr_put, &wr, idx, w, h, bpp, pal, pal_n) != 0 && !wr.err) wr.err = EIO;
    return close_wr(&wr);
}
