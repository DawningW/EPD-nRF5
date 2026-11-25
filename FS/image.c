#include "image.h"

#include <stdlib.h>
#include <string.h>

void rle_init(rle_ctx_t* ctx, uint32_t expected_len, uint8_t bpp, on_data_callback callback, void* user_data) {
    memset(ctx, 0, sizeof(rle_ctx_t));
    ctx->expected_len = expected_len;
    ctx->bpp = bpp;
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->state = RLE_STATE_OPCODE;
}

static void emit_pixel(rle_ctx_t* ctx, uint8_t val) {
    if (val) {
        ctx->current_byte |= (val << (8 - ctx->bpp - ctx->bit_count));
    }
    ctx->bit_count += ctx->bpp;
    if (ctx->bit_count == 8) {
        ctx->buffer[ctx->buf_pos++] = ctx->current_byte;
        ctx->current_byte = 0;
        ctx->bit_count = 0;

        if (ctx->buf_pos == sizeof(ctx->buffer)) {
            ctx->callback(ctx->buffer, ctx->buf_pos, ctx->user_data);
            ctx->buf_pos = 0;
        }
    }
    ctx->decoded_len++;
}

uint32_t rle_decode(rle_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    uint32_t i = 0;
    uint8_t cbits = 6 - ctx->bpp;
    while (ctx->decoded_len < ctx->expected_len) {
        if (ctx->state == RLE_STATE_EMIT) {
            while (ctx->rle_count > 0) {
                emit_pixel(ctx, ctx->rle_pixel_val);
                ctx->rle_count--;
                if (ctx->decoded_len >= ctx->expected_len) break;
            }

            if (ctx->rle_count == 0) {
                ctx->state = RLE_STATE_OPCODE;
            }
            continue;
        }

        if (i >= len) break;  // Need more input
        uint8_t byte = data[i++];

        if (ctx->state == RLE_STATE_OPCODE) {
            if ((byte & 0x80) == 0) {
                // Literal (0xxxxxxx)
                for (int k = 0; k < 7 / ctx->bpp; k++) {
                    if (ctx->decoded_len >= ctx->expected_len) break;

                    uint8_t pixel = (byte >> (7 - ctx->bpp * (k + 1))) & ((1 << ctx->bpp) - 1);
                    emit_pixel(ctx, pixel);
                }
            } else {
                // RLE (1xxxxxxx)
                ctx->rle_pixel_val = (byte >> (cbits + 1)) & ((1 << ctx->bpp) - 1);
                uint8_t has_next = (byte >> cbits) & 1;
                ctx->rle_count = byte & ((1 << cbits) - 1);

                if (has_next) {
                    ctx->state = RLE_STATE_READ_COUNT;
                    ctx->rle_shift = cbits;
                } else {
                    ctx->rle_count += 8;
                    ctx->state = RLE_STATE_EMIT;
                }
            }
        } else if (ctx->state == RLE_STATE_READ_COUNT) {
            uint8_t has_next = (byte & 0x80) >> 7;
            uint8_t part = byte & 0x7F;
            ctx->rle_count |= ((uint32_t)part << ctx->rle_shift);
            ctx->rle_shift += 7;

            if (!has_next) {
                ctx->rle_count += 8;
                ctx->state = RLE_STATE_EMIT;
            }
        }
    }

    if (ctx->decoded_len == ctx->expected_len) {
        if (ctx->bit_count > 0) {
            ctx->buffer[ctx->buf_pos++] = ctx->current_byte;
            ctx->bit_count = 0;
            ctx->current_byte = 0;
        }
        if (ctx->buf_pos > 0) {
            ctx->callback(ctx->buffer, ctx->buf_pos, ctx->user_data);
            ctx->buf_pos = 0;
        }
    }

    return i;
}
