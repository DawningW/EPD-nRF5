/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include "EPD_service.h"

#include <string.h>

#include "app_scheduler.h"
#include "ble_srv_common.h"
#include "main.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "sdk_macros.h"
#if FILESYSTEM_ENABLED
#include "filesystem.h"
#include "image.h"
#endif

#if defined(S112)
#define EPD_CFG_52811 {0x14, 0x13, 0x06, 0x05, 0x04, 0x03, 0x02, 0x02, 0xFF, 0x12, 0x07}
#define EPD_CFG_52810 {0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x02, 0xFF, 0x0D, 0x02}
#else
#define EPD_CFG_DEFAULT {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x03, 0x09, 0x03}
// #define EPD_CFG_DEFAULT {0x05, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x01, 0x07}
#endif

#if FILESYSTEM_ENABLED
typedef struct {
    epd_model_t* epd;
    uint8_t plane;
    bool first;
} image_ctx_t;

static File dl_file;
static bool dl_active = false;

static void on_image_data(const uint8_t* data, uint32_t len, void* user_data) {
    image_ctx_t* img = (image_ctx_t*)user_data;
    // plane 0: black, plane 1: color (BWR only); "first" marks the start of a plane
    img->epd->drv->write_ram(img->epd, img->first, img->plane == 0, (uint8_t*)data, (uint8_t)len);
    img->first = false;
}

static bool epd_draw_image(const char* filename, epd_model_t* epd) {
    char path[LFS_NAME_MAX + 1] = {0};
    snprintf(path, sizeof(path), IMAGE_PATH "/%s", filename);
    File file;
    if (FS_Open(&file, path, FS_O_RDONLY) < 0) {
        return false;
    }

    uint8_t bpp = epd->color == COLOR_BWRY ? 2 : 1;
    uint8_t planes = epd->color == COLOR_BWR ? 2 : 1;
    uint32_t pixels = epd->width * epd->height;

    epd->drv->clear(epd, false);

    rle_ctx_t ctx;
    image_ctx_t img = {.epd = epd};
    uint8_t buf[256];
    uint32_t off = 0;
    int read_len = 0;

    for (uint8_t plane = 0; plane < planes; plane++) {
        rle_init(&ctx, pixels, bpp, on_image_data, &img);
        img.plane = plane;
        img.first = true;

        while (1) {
            if (off >= read_len) {
                read_len = FS_Read(&file, buf, sizeof(buf));
                off = 0;
                if (read_len <= 0) break;
                app_feed_wdt();
            }
            off += rle_decode(&ctx, buf + off, read_len - off);
            if (ctx.decoded_len >= ctx.expected_len) break;
        }

        if (ctx.decoded_len < ctx.expected_len) break;
    }

    FS_Close(&file);
    return true;
}

static bool epd_image_update(epd_config_t* config, epd_model_t* epd, bool force) {
    uint8_t mode = config->gallery_mode;
    if (mode == GALLERY_MODE_RANDOM) srand(timestamp());

    Directory dir;
    if (FS_OpenDir(&dir, IMAGE_PATH) != 0) return false;

    char first[LFS_NAME_MAX + 1] = {0};
    char pick[LFS_NAME_MAX + 1] = {0};
    uint16_t image_count = 0;
    uint16_t random_count = 0;
    bool current_found = false;
    int ret = 0;
    FileInfo info;

    while ((ret = FS_ReadDir(&dir, &info)) > 0) {
        if (info.type != LFS_TYPE_REG) continue;
        image_count++;
        if (!first[0]) strcpy(first, info.name);

        if (strcmp(info.name, config->gallery_image) == 0) {
            current_found = true;
            continue;
        }

        if (mode == GALLERY_MODE_RANDOM) {
            random_count++;
            if (rand() % random_count == 0) strcpy(pick, info.name);
        } else if (mode == GALLERY_MODE_SEQ && current_found && !pick[0]) {
            strcpy(pick, info.name);
        }
    }
    FS_CloseDir(&dir);

    if (ret < 0 || image_count == 0) return false;

    if (image_count == 1) {
        strcpy(pick, first);
    } else if (mode == GALLERY_MODE_FIXED) {
        strcpy(pick, current_found ? config->gallery_image : first);
    } else if (!pick[0]) {
        strcpy(pick, first);
    }

    bool image_changed = strcmp(pick, config->gallery_image) != 0;
    if (!force && !image_changed) return true;

    if (!epd_draw_image(pick, epd)) return true;
    if (image_changed) {
        strcpy(config->gallery_image, pick);
        epd_config_write(config);
    }
    epd->drv->refresh(epd);
    return true;
}

void ble_epd_file_chunk_send(ble_epd_t* p_epd, File* file) {
    static uint8_t buf[BLE_EPD_MAX_DATA_LEN];
    int read_len = FS_Read(file, buf, p_epd->max_data_len);
    if (read_len <= 0) {
        FS_Close(file);
        dl_active = false;
        return;
    }
    ble_epd_string_send(p_epd, buf, read_len);
}
#endif

static uint32_t rle_decompress_from(const uint8_t* src, uint32_t src_len, uint32_t* src_pos, uint8_t* dst,
                                    uint32_t dst_len) {
    uint32_t dst_pos = 0;
    while (*src_pos < src_len && dst_pos < dst_len) {
        uint8_t control = src[*src_pos];  // peek, don't consume yet
        if (control & 0x80) {             // repeat run
            uint32_t count = (control & 0x7F) + 3;
            if (*src_pos + 1 >= src_len) break;    // need value byte
            if (dst_pos + count > dst_len) break;  // won't fit — retry later
            (*src_pos)++;                          // consume control
            (*src_pos)++;                          // consume value
            uint8_t value = src[*src_pos - 1];
            for (uint32_t j = 0; j < count; j++) dst[dst_pos++] = value;
        } else {  // literal run
            uint32_t count = control + 1;
            if (*src_pos + 1 + count > src_len) break;  // need all literal bytes
            if (dst_pos + count > dst_len) break;       // won't fit — retry later
            (*src_pos)++;                               // consume control
            for (uint32_t j = 0; j < count; j++) dst[dst_pos++] = src[(*src_pos)++];
        }
    }
    return dst_pos;
}

static void epd_gui_update(void* p_event_data, uint16_t event_size) {
    epd_gui_update_event_t* event = (epd_gui_update_event_t*)p_event_data;
    ble_epd_t* p_epd = event->p_epd;

    EPD_GPIO_Init();
    epd_model_t* epd = epd_init((epd_model_id_t)p_epd->config.model_id);
    display_mode_t display_mode = (display_mode_t)p_epd->config.display_mode;
    if (display_mode == MODE_GALLERY) {
#if FILESYSTEM_ENABLED
        if (!epd_image_update(&p_epd->config, epd, event->force)) {
            gui_data_t data = {
                .mode = display_mode,
                .color = epd->color,
                .width = epd->width,
                .height = epd->height,
            };
            DrawGUI(&data, (buffer_callback)epd->drv->write_image, epd);
            epd->drv->refresh(epd);
        }
#endif
    } else {
        gui_data_t data = {
            .mode = display_mode,
            .color = epd->color,
            .width = epd->width,
            .height = epd->height,
            .timestamp = event->timestamp,
            .week_start = p_epd->config.week_start,
            .temperature = epd->drv->read_temp(epd),
            .voltage = EPD_ReadVoltage(),
        };

        uint16_t dev_name_len = sizeof(data.ssid);
        uint32_t err_code = sd_ble_gap_device_name_get((uint8_t*)data.ssid, &dev_name_len);
        if (err_code == NRF_SUCCESS && dev_name_len > 0) data.ssid[dev_name_len] = '\0';

        DrawGUI(&data, (buffer_callback)epd->drv->write_image, epd);
        epd->drv->refresh(epd);
        epd->drv->sleep(epd);
        nrf_delay_ms(200);  // for sleep
    }
    EPD_GPIO_Uninit();

    app_feed_wdt();
}

/**@brief Function for handling the @ref BLE_GAP_EVT_CONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_connect(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    p_epd->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    EPD_GPIO_Init();
}

/**@brief Function for handling the @ref BLE_GAP_EVT_DISCONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_disconnect(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    UNUSED_PARAMETER(p_ble_evt);
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID;
#if FILESYSTEM_ENABLED
    if (dl_active) {
        FS_Close(&dl_file);
        dl_active = false;
    }
#endif
    if (p_epd->epd != NULL) {
        p_epd->epd->drv->sleep(p_epd->epd);
        p_epd->epd = NULL;
        nrf_delay_ms(200);  // for sleep
    }
    EPD_GPIO_Uninit();
}

static void epd_update_display_mode(ble_epd_t* p_epd, display_mode_t mode) {
    if (p_epd->config.display_mode != mode) {
        p_epd->config.display_mode = mode;
        epd_config_write(&p_epd->config);
    }
}

static void epd_send_time(ble_epd_t* p_epd) {
    char buf[20] = {0};
    snprintf(buf, 20, "t=%" PRIu32, timestamp());
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

static void epd_send_mtu(ble_epd_t* p_epd) {
    char buf[20] = {0};
    snprintf(buf, sizeof(buf), "mtu=%d rle=1", p_epd->max_data_len);
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

static void epd_service_on_write(ble_epd_t* p_epd, uint8_t* p_data, uint16_t length) {
    NRF_LOG_DEBUG("[EPD]: on_write LEN=%d\n", length);
    NRF_LOG_HEXDUMP_DEBUG(p_data, length);
    if (p_data == NULL || length <= 0) return;

    switch (p_data[0]) {
        case EPD_CMD_SET_PINS:
            if (length < 8) return;

            p_epd->config.mosi_pin = p_data[1];
            p_epd->config.sclk_pin = p_data[2];
            p_epd->config.cs_pin = p_data[3];
            p_epd->config.dc_pin = p_data[4];
            p_epd->config.rst_pin = p_data[5];
            p_epd->config.busy_pin = p_data[6];
            p_epd->config.bs_pin = p_data[7];
            if (length > 8) p_epd->config.en_pin = p_data[8];
            epd_config_write(&p_epd->config);

            EPD_GPIO_Uninit();
            EPD_GPIO_Load(&p_epd->config);
            EPD_GPIO_Init();
            break;

        case EPD_CMD_INIT:
            p_epd->epd = epd_init((epd_model_id_t)(length > 1 ? p_data[1] : p_epd->config.model_id));
            if (p_epd->epd->id != p_epd->config.model_id) {
                p_epd->config.model_id = p_epd->epd->id;
                epd_config_write(&p_epd->config);
            }
            epd_send_mtu(p_epd);
            epd_send_time(p_epd);
            break;

        case EPD_CMD_CLEAR:
            epd_update_display_mode(p_epd, MODE_PICTURE);
            if (p_epd->epd) {
                p_epd->epd->drv->init(p_epd->epd);
                p_epd->epd->drv->clear(p_epd->epd, length > 1 ? p_data[1] : true);
            }
            break;

        case EPD_CMD_SEND_COMMAND:
            if (length < 2) return;
            EPD_WriteCmd(p_data[1]);
            break;

        case EPD_CMD_SEND_DATA:
            EPD_WriteData(&p_data[1], length - 1);
            break;

        case EPD_CMD_REFRESH:
            epd_update_display_mode(p_epd, MODE_PICTURE);
            if (p_epd->epd) p_epd->epd->drv->refresh(p_epd->epd);
            break;

        case EPD_CMD_SLEEP:
            if (p_epd->epd) p_epd->epd->drv->sleep(p_epd->epd);
            break;

        case EPD_CMD_SET_TIME: {
            if (length < 5) return;

            NRF_LOG_DEBUG("time: %02x %02x %02x %02x\n", p_data[1], p_data[2], p_data[3], p_data[4]);
            if (length > 5) NRF_LOG_DEBUG("timezone: %d\n", (int8_t)p_data[5]);

            uint32_t timestamp = (p_data[1] << 24) | (p_data[2] << 16) | (p_data[3] << 8) | p_data[4];
            timestamp += (length > 5 ? (int8_t)p_data[5] : 8) * 60 * 60;  // timezone
            set_timestamp(timestamp);
            epd_update_display_mode(p_epd, length > 6 ? (display_mode_t)p_data[6] : MODE_CALENDAR);
            ble_epd_on_timer(p_epd, timestamp, true);
        } break;

        case EPD_CMD_SET_WEEK_START:
            if (length < 2) return;
            if (p_data[1] < 7 && p_data[1] != p_epd->config.week_start) {
                p_epd->config.week_start = p_data[1];
                epd_config_write(&p_epd->config);
            }
            break;

        case EPD_CMD_WRITE_IMAGE: {
            if (length < 3) return;

            // BIT 0: black/red, BIT 1: begin, BIT 2: rle
            bool black = (p_data[1] & 0x01) == 0;
            bool begin = (p_data[1] & 0x02) != 0;
            bool rle = (p_data[1] & 0x04) != 0;

            uint16_t data_len = length - 2;
            uint8_t rle_out[UINT8_MAX];
            uint32_t src_pos = 0;

            if (rle) {
                while (src_pos < data_len) {
                    app_feed_wdt();
                    uint32_t out_len = rle_decompress_from(&p_data[2], data_len, &src_pos, rle_out, sizeof(rle_out));
                    if (out_len == 0) break;
                    if (p_epd->epd) p_epd->epd->drv->write_ram(p_epd->epd, begin, black, rle_out, (uint16_t)out_len);
                    begin = false;
                }
            } else {
                if (p_epd->epd) p_epd->epd->drv->write_ram(p_epd->epd, begin, black, &p_data[2], data_len);
            }
        } break;

#if FILESYSTEM_ENABLED
        case EPD_CMD_SHOW_IMAGE: {
            if (length < 2) return;
            epd_update_display_mode(p_epd, MODE_GALLERY);
            char filename[LFS_NAME_MAX + 1] = {0};
            memcpy(filename, &p_data[1], MIN(length - 1, LFS_NAME_MAX));
            if (epd_draw_image(filename, p_epd->epd)) {
                strcpy(p_epd->config.gallery_image, filename);
                epd_config_write(&p_epd->config);
                p_epd->epd->drv->refresh(p_epd->epd);
            }
        } break;

        case EPD_CMD_SET_GALLERY_MODE:
            if (length < 2) return;
            if (p_data[1] <= GALLERY_MODE_RANDOM && p_data[1] != p_epd->config.gallery_mode) {
                p_epd->config.gallery_mode = p_data[1];
                epd_config_write(&p_epd->config);
            }
            break;

        case EPD_CMD_FSSTAT: {
            FSInfo info = {0};
            FS_Stat(&info);
            ble_epd_string_send(p_epd, (uint8_t*)&info, sizeof(info));
        } break;

        case EPD_CMD_DIR: {
            if (length < 2) return;
            char filename[LFS_NAME_MAX + 1] = {0};
            memcpy(filename, &p_data[1], MIN(length - 1, LFS_NAME_MAX));
            Directory dir;
            if (FS_OpenDir(&dir, filename) == 0) {
                FileInfo info;
                while (FS_ReadDir(&dir, &info) > 0) {
                    ble_epd_string_send(p_epd, (uint8_t*)&info, sizeof(info));
                    nrf_delay_ms(5);
                }
                FS_CloseDir(&dir);
            }
        } break;

        case EPD_CMD_UPLOAD: {
            if (length < 2) return;
            static File* p_file = NULL;
            if (p_data[1] == 0) { // Open
                if (p_file != NULL) {
                    FS_Close(p_file);
                    free(p_file);
                    p_file = NULL;
                }
                char filename[LFS_NAME_MAX + 1] = {0};
                memcpy(filename, &p_data[2], MIN(length - 2, LFS_NAME_MAX));
                p_file = malloc(sizeof(File));
                memset(p_file, 0, sizeof(File));
                if (FS_Open(p_file, filename, FS_O_CREAT | FS_O_WRONLY | FS_O_TRUNC) < 0) {
                    free(p_file);
                    p_file = NULL;
                }
            } else if (p_data[1] == 1) { // Write
                if (p_file != NULL) {
                    FS_Write(p_file, &p_data[2], length - 2);
                }
            } else if (p_data[1] == 2) { // Close
                if (p_file != NULL) {
                    FS_Close(p_file);
                    free(p_file);
                    p_file = NULL;
                }
            }
        } break;

        case EPD_CMD_DOWNLOAD: {
            if (length < 2) return;
            char filename[LFS_NAME_MAX + 1] = {0};
            memcpy(filename, &p_data[1], MIN(length - 1, LFS_NAME_MAX));
            if (dl_active) FS_Close(&dl_file);
            if (FS_Open(&dl_file, filename, FS_O_RDONLY) != 0) return;
            dl_active = true;
            ble_epd_file_chunk_send(p_epd, &dl_file);
        } break;

        case EPD_CMD_DELETE: {
            if (length < 2) return;
            char filename[LFS_NAME_MAX + 1] = {0};
            memcpy(filename, &p_data[1], MIN(length - 1, LFS_NAME_MAX));
            FS_Remove(filename);
        } break;

        case EPD_CMD_FORMAT:
            FS_Format();
            break;
#endif

        case EPD_CMD_SET_CONFIG:
            if (length < 2) return;
            memcpy(&p_epd->config, &p_data[1], (length - 1 > EPD_CONFIG_SIZE) ? EPD_CONFIG_SIZE : length - 1);
            if (!FILESYSTEM_ENABLED) p_epd->config.gallery_mode = 0xFF;
            epd_config_write(&p_epd->config);
            break;

        case EPD_CMD_SYS_SLEEP:
            sleep_mode_enter();
            break;

        case EPD_CMD_SYS_RESET:
#if defined(S112)
            nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
#else
            NVIC_SystemReset();
#endif
            break;

        case EPD_CMD_CFG_ERASE:
            epd_config_clear(&p_epd->config);
            nrf_delay_ms(100);  // required
            NVIC_SystemReset();
            break;

        default:
            break;
    }
}

/**@brief Function for handling the @ref BLE_GATTS_EVT_WRITE event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_write(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    ble_gatts_evt_write_t* p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if ((p_evt_write->handle == p_epd->char_handles.cccd_handle) && (p_evt_write->len == 2)) {
        if (ble_srv_is_notification_enabled(p_evt_write->data)) {
            NRF_LOG_DEBUG("notification enabled\n");
            p_epd->is_notification_enabled = true;
            static uint16_t length = sizeof(epd_config_t);
            NRF_LOG_DEBUG("send epd config\n");
            uint32_t err_code = ble_epd_string_send(p_epd, (uint8_t*)&p_epd->config, length);
            if (err_code != NRF_ERROR_INVALID_STATE) APP_ERROR_CHECK(err_code);
        } else {
            p_epd->is_notification_enabled = false;
        }
    } else if (p_evt_write->handle == p_epd->char_handles.value_handle) {
        epd_service_on_write(p_epd, p_evt_write->data, p_evt_write->len);
    } else {
        // Do Nothing. This event is not relevant for this service.
    }
}

#if defined(S112)
void ble_epd_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    if (p_context == NULL || p_ble_evt == NULL) return;

    ble_epd_t* p_epd = (ble_epd_t*)p_context;
    ble_epd_on_ble_evt(p_epd, (ble_evt_t*)p_ble_evt);
}
#endif

void ble_epd_on_ble_evt(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    if ((p_epd == NULL) || (p_ble_evt == NULL)) {
        return;
    }

    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_epd, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_epd, p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            on_write(p_epd, p_ble_evt);
            break;

#if FILESYSTEM_ENABLED
        case BLE_GATTS_EVT_HVN_TX_COMPLETE:
            if (dl_active) ble_epd_file_chunk_send(p_epd, &dl_file);
            break;
#endif

        default:
            // No implementation needed.
            break;
    }
}

static uint32_t epd_service_init(ble_epd_t* p_epd) {
    ble_uuid_t ble_uuid = {0};
    ble_uuid128_t base_uuid = BLE_UUID_EPD_SVC_BASE;
    ble_add_char_params_t add_char_params;
    uint8_t app_version = APP_VERSION;

    VERIFY_SUCCESS(sd_ble_uuid_vs_add(&base_uuid, &ble_uuid.type));

    ble_uuid.type = ble_uuid.type;
    ble_uuid.uuid = BLE_UUID_EPD_SVC;
    VERIFY_SUCCESS(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &ble_uuid, &p_epd->service_handle));

    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid = BLE_UUID_EPD_CHAR;
    add_char_params.uuid_type = ble_uuid.type;
    add_char_params.max_len = BLE_EPD_MAX_DATA_LEN;
    add_char_params.init_len = sizeof(uint8_t);
    add_char_params.is_var_len = true;
    add_char_params.char_props.notify = 1;
    add_char_params.char_props.write = 1;
    add_char_params.char_props.write_wo_resp = 1;
    add_char_params.read_access = SEC_OPEN;
    add_char_params.write_access = SEC_OPEN;
    add_char_params.cccd_write_access = SEC_OPEN;

    VERIFY_SUCCESS(characteristic_add(p_epd->service_handle, &add_char_params, &p_epd->char_handles));

    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid = BLE_UUID_APP_VER;
    add_char_params.uuid_type = ble_uuid.type;
    add_char_params.max_len = sizeof(uint8_t);
    add_char_params.init_len = sizeof(uint8_t);
    add_char_params.p_init_value = &app_version;
    add_char_params.char_props.read = 1;
    add_char_params.read_access = SEC_OPEN;

    return characteristic_add(p_epd->service_handle, &add_char_params, &p_epd->app_ver_handles);
}

void ble_epd_sleep_prepare(ble_epd_t* p_epd) {
    // Turn off led
    EPD_LED_OFF();
    // Prepare wakeup pin
    if (p_epd->config.wakeup_pin != 0xFF) {
        nrf_gpio_cfg_sense_input(p_epd->config.wakeup_pin, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
    }
}

uint32_t ble_epd_init(ble_epd_t* p_epd) {
    if (p_epd == NULL) return NRF_ERROR_NULL;

    // Initialize the service structure.
    p_epd->max_data_len = BLE_EPD_MAX_DATA_LEN;
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID;
    p_epd->is_notification_enabled = false;

    epd_config_init(&p_epd->config);
    epd_config_read(&p_epd->config);

    // write default config
    if (epd_config_empty(&p_epd->config)) {
#if defined(S112)
        if (NRF_FICR->INFO.PART == 0x52810) {
            uint8_t cfg[] = EPD_CFG_52810;
            memcpy(&p_epd->config, cfg, sizeof(cfg));
        } else {
            uint8_t cfg[] = EPD_CFG_52811;
            memcpy(&p_epd->config, cfg, sizeof(cfg));
        }
#else
        uint8_t cfg[] = EPD_CFG_DEFAULT;
        memcpy(&p_epd->config, cfg, sizeof(cfg));
#endif
        if (p_epd->config.display_mode == 0xFF) p_epd->config.display_mode = MODE_CALENDAR;
        if (p_epd->config.week_start == 0xFF) p_epd->config.week_start = 0;
        epd_config_write(&p_epd->config);
    }

    if (!FILESYSTEM_ENABLED) {
        p_epd->config.gallery_mode = 0xFF;
    } else if (p_epd->config.gallery_mode == 0xFF) {
        p_epd->config.gallery_mode = GALLERY_MODE_SEQ;
    }

    // load config
    EPD_GPIO_Load(&p_epd->config);

    // blink LED on start
    EPD_LED_BLINK();

    // Add the service.
    return epd_service_init(p_epd);
}

uint32_t ble_epd_string_send(ble_epd_t* p_epd, uint8_t* p_string, uint16_t length) {
    if ((p_epd->conn_handle == BLE_CONN_HANDLE_INVALID) || (!p_epd->is_notification_enabled))
        return NRF_ERROR_INVALID_STATE;
    if (length > p_epd->max_data_len) return NRF_ERROR_INVALID_PARAM;

    ble_gatts_hvx_params_t hvx_params;

    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = p_epd->char_handles.value_handle;
    hvx_params.p_data = p_string;
    hvx_params.p_len = &length;
    hvx_params.type = BLE_GATT_HVX_NOTIFICATION;

    return sd_ble_gatts_hvx(p_epd->conn_handle, &hvx_params);
}

void ble_epd_on_timer(ble_epd_t* p_epd, uint32_t timestamp, bool force_update) {
    // Update calendar/gallery on 00:00:00, clock on every minute
    if (force_update || (p_epd->config.display_mode == MODE_CALENDAR && timestamp % 86400 == 0) ||
#if FILESYSTEM_ENABLED
        (p_epd->config.display_mode == MODE_GALLERY && timestamp % 86400 == 0) ||
#endif
        (p_epd->config.display_mode == MODE_CLOCK && timestamp % 60 == 0)) {
        epd_gui_update_event_t event = {p_epd, timestamp, force_update};
        app_sched_event_put(&event, sizeof(epd_gui_update_event_t), epd_gui_update);
    }
}
