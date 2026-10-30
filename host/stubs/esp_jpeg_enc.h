#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum { JPEG_ERR_OK = 0, JPEG_ERR_FAIL = -1 } jpeg_error_t;
typedef enum { JPEG_PIXEL_FORMAT_RGB888 = 1 } jpeg_pixel_format_t;
typedef enum { JPEG_SUBSAMPLE_444 = 1 } jpeg_subsampling_t;
typedef struct { int width, height; jpeg_pixel_format_t src_type; jpeg_subsampling_t subsampling; uint8_t quality;
                 bool task_enable; uint8_t hfm_task_priority, hfm_task_core; } jpeg_enc_config_t;
typedef void *jpeg_enc_handle_t;
#define DEFAULT_JPEG_ENC_CONFIG() {0}
static inline jpeg_error_t jpeg_enc_open(jpeg_enc_config_t *c, jpeg_enc_handle_t *h) { (void)c; (void)h; return JPEG_ERR_FAIL; }
static inline jpeg_error_t jpeg_enc_process(jpeg_enc_handle_t h, const uint8_t *i, int il, uint8_t *o, int ol, int *n) { return JPEG_ERR_FAIL; }
static inline jpeg_error_t jpeg_enc_close(jpeg_enc_handle_t h) { return JPEG_ERR_OK; }
