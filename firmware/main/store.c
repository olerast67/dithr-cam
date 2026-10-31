// Карта памяти в режиме 1 бит (GPIO 14/15/2) и запись файлов.
// Данные из PSRAM карта по DMA забрать не может, поэтому пишем кусками по
// 16 КБ через внутренний буфер: так быстрее, чем побайтовая stdio.
#include "store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "fx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

static const char *TAG = "store";
#define CHUNK (16 * 1024)

static sdmmc_card_t *s_card;
static uint8_t *s_bounce;
static uint32_t s_shot = 1;
static char s_err[24];  // что сломалось при последней записи, для экрана

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

static int s_khz;
static store_state_t s_state = STORE_NO_CARD;

static FATFS *s_fs;
static BYTE s_pdrv = 0xFF;
static char s_drv[3];

// Карта инициализируется один раз при запуске, пока экран не занял GPIO13.
// Это линия D3 карты: если при повторной инициализации (CMD0) на ней низкий
// уровень от передачи I2C, карта уходит в режим SPI и перестаёт отвечать.
// Поэтому дальше монтируем и форматируем через FatFs, не трогая карту.
static esp_err_t card_init(int khz) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = khz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = host.init();
    if (err != ESP_OK) return err;
    if (!s_card) s_card = calloc(1, sizeof(*s_card));
    if (!s_card) return ESP_ERR_NO_MEM;
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, s_card);
    if (err != ESP_OK) host.deinit();
    return err;
}

// FatFs поверх уже готовой карты. ESP_FAIL: файловой системы FAT на карте нет.
static esp_err_t fs_mount(void) {
    if (!s_fs) {
        if (ff_diskio_get_drive(&s_pdrv) != ESP_OK) return ESP_ERR_NO_MEM;
        ff_diskio_register_sdmmc(s_pdrv, s_card);
        s_drv[0] = (char)('0' + s_pdrv);
        s_drv[1] = ':';
        const esp_vfs_fat_conf_t conf = {.base_path = "/sdcard", .fat_drive = s_drv, .max_files = 3};
        const esp_err_t err = esp_vfs_fat_register(&conf, &s_fs);
        if (err != ESP_OK) return err;
    }
    const FRESULT fr = f_mount(s_fs, s_drv, 1);
    if (fr != FR_OK) ESP_LOGW(TAG, "f_mount: FatFs error %d", fr);
    return fr == FR_OK ? ESP_OK : ESP_FAIL;
}

static void fs_unmount(void) { f_mount(NULL, s_drv, 0); }

// Изношенная карта (часто из видеорегистратора) уходит в «только чтение» и
// молча игнорирует запись, а FatFs видит свой кэш и считает, что всё записано.
// Поэтому пишем случайное число, перемонтируем карту и читаем его с карты.
static store_state_t write_check(void) {
    const char *p = STORE_DIR "/PROBE.TMP";
    const uint32_t token = esp_random();
    uint32_t got = ~token;
    FILE *f = fopen(p, "wb");
    if (!f) return STORE_READ_ONLY;
    const bool wrote = fwrite(&token, 4, 1, f) == 1;
    if (fclose(f) != 0 || !wrote) return STORE_READ_ONLY;
    fs_unmount();  // сброс кэша FatFs: читаем уже с карты
    if (fs_mount() != ESP_OK) return STORE_NO_CARD;
    f = fopen(p, "rb");
    if (f) {
        fread(&got, 4, 1, f);
        fclose(f);
    }
    unlink(p);
    return got == token ? STORE_OK : STORE_READ_ONLY;
}

static void scan_shots(void) {
    // Номер в NVS мог сброситься (стирание flash, новая версия NVS). Чтобы не
    // затереть старые снимки, продолжаем после самого большого номера на карте.
    DIR *d = opendir(STORE_DIR);
    if (!d) return;
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

// Карта уже смонтирована: папка, проверка записи, нумерация.
static store_state_t prepare(void) {
    sdmmc_card_print_info(stdout, s_card);
    if (mkdir(STORE_DIR, 0777) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s: errno %d", STORE_DIR, errno);
        return STORE_READ_ONLY;
    }
    const store_state_t st = write_check();
    if (st != STORE_OK) {
        ESP_LOGE(TAG, "card does not keep written data (read-only)");
        return st;
    }
    scan_shots();
    return s_bounce ? STORE_OK : STORE_NO_MEM;
}

esp_err_t store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    s_shot = nvs_get("shot", 1);
    s_bounce = heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    // 20 МГц: стандартная частота, с ней работают все карты.
    s_khz = SDMMC_FREQ_DEFAULT;
    err = card_init(s_khz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD at 20 MHz: %s, retry 10 MHz", esp_err_to_name(err));
        s_khz = 10000;
        err = card_init(s_khz);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no SD card: %s", esp_err_to_name(err));
        s_state = STORE_NO_CARD;
        return err;
    }
    err = fs_mount();
    if (err == ESP_FAIL) {
        // Карта отвечает, но FAT на ней нет: обычно exFAT на картах от 64 ГБ.
        ESP_LOGW(TAG, "card has no FAT filesystem (exFAT?)");
        s_state = STORE_NOT_FAT;
        return err;
    }
    if (err != ESP_OK) {
        s_state = STORE_NO_MEM;
        return err;
    }
    s_state = prepare();
    return s_state == STORE_OK ? ESP_OK : ESP_FAIL;
}

// Формат своими силами: штатный в ESP-IDF пишет FAT кусками по 4 КБ и две
// копии, на карте 64 ГБ это ~15 МБ мелкими записями. Здесь одна копия FAT и
// буфер 32 КБ: запросов к карте в 16 раз меньше.
#define FMT_WORK (32 * 1024)

esp_err_t store_format(void) {
    if (s_state != STORE_NOT_FAT || !s_fs) return ESP_ERR_INVALID_STATE;
    const int64_t t0 = esp_timer_get_time();
    uint8_t *work = heap_caps_malloc(FMT_WORK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!work) return ESP_ERR_NO_MEM;
    fs_unmount();
    ESP_LOGI(TAG, "format: card %llu MB, partitioning",
             (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    const LBA_t plist[] = {100, 0, 0, 0};  // один раздел на всю карту
    FRESULT fr = f_fdisk(s_pdrv, plist, work);
    if (fr == FR_OK) {
        ESP_LOGI(TAG, "format: mkfs FAT32");
        const MKFS_PARM opt = {FM_FAT32, 1, 0, 0, 32 * 1024};
        fr = f_mkfs(s_drv, &opt, work, FMT_WORK);
    }
    heap_caps_free(work);
    ESP_LOGI(TAG, "format: FatFs result %d in %lld ms", fr, (long long)(esp_timer_get_time() - t0) / 1000);
    if (fr != FR_OK) return ESP_FAIL;
    const esp_err_t err = fs_mount();
    if (err != ESP_OK) return err;
    s_state = prepare();
    return s_state == STORE_OK ? ESP_OK : ESP_FAIL;
}

store_state_t store_state(void) { return s_state; }

bool store_ok(void) { return s_state == STORE_OK; }

const char *store_last_error(void) { return s_err[0] ? s_err : "?"; }

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
        const ssize_t n = write(w->fd, s_bounce, w->fill);
        if (n != (ssize_t)w->fill) {
            w->err = errno ? errno : EIO;
            ESP_LOGE(TAG, "write %u bytes -> %d, errno %d", (unsigned)w->fill, (int)n, w->err);
            snprintf(s_err, sizeof(s_err), "WRITE E%d", w->err);
        }
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
        ESP_LOGE(TAG, "open %s: errno %d", p, errno);
        snprintf(s_err, sizeof(s_err), "OPEN E%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t close_wr(wr_t *w) {
    wr_flush(w);
    // FAT дописывает последний сектор и запись каталога только в close():
    // если карта отвалилась здесь, файл обрезан, и это ошибка.
    if (close(w->fd) != 0 && !w->err) {
        w->err = errno ? errno : EIO;
        ESP_LOGE(TAG, "close: errno %d", w->err);
        snprintf(s_err, sizeof(s_err), "CLOSE E%d", w->err);
    }
    return w->err ? ESP_FAIL : ESP_OK;
}

esp_err_t store_save(uint32_t shot, const char *tag, const char *ext, const uint8_t *buf, size_t len) {
    if (s_state != STORE_OK) return ESP_ERR_INVALID_STATE;
    wr_t w;
    if (open_wr(&w, shot, tag, ext) != ESP_OK) return ESP_FAIL;
    wr_put(&w, buf, len);
    return close_wr(&w);
}

esp_err_t store_save_png(uint32_t shot, const char *tag, const uint8_t *idx, int w, int h, int bpp,
                         const uint8_t (*pal)[3], int pal_n) {
    if (s_state != STORE_OK) return ESP_ERR_INVALID_STATE;
    wr_t wr;
    if (open_wr(&wr, shot, tag, "png") != ESP_OK) return ESP_FAIL;
    if (fx_png_write_indexed(wr_put, &wr, idx, w, h, bpp, pal, pal_n) != 0 && !wr.err) wr.err = EIO;
    return close_wr(&wr);
}
