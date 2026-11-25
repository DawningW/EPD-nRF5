#ifndef __IMAGE_H
#define __IMAGE_H

#include <stdint.h>

typedef void (*on_data_callback)(const uint8_t* data, uint32_t len, void* user_data);

typedef enum {
    RLE_STATE_OPCODE = 0,
    RLE_STATE_READ_COUNT,
    RLE_STATE_EMIT
} rle_state_t;

typedef struct {
    uint32_t expected_len;
    uint32_t decoded_len;
    on_data_callback callback;
    void* user_data;

    uint8_t buffer[64];
    uint8_t buf_pos;

    uint8_t bpp;

    uint8_t state; // rle_state_t

    uint8_t rle_pixel_val;
    uint32_t rle_count;
    uint8_t rle_shift;

    uint8_t current_byte;
    uint8_t bit_count;
} rle_ctx_t;

void rle_init(rle_ctx_t* ctx, uint32_t expected_len, uint8_t bpp, on_data_callback callback, void* user_data);
uint32_t rle_decode(rle_ctx_t* ctx, const uint8_t* data, uint32_t len);

#endif
