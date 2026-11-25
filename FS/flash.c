#include "flash.h"

#include "app_error.h"
#include "nrf_drv_spi.h"
#include "nrf_delay.h"
#include "nrf_log.h"

#if SPIFLASH_ENABLED
static const spiflash_params_t m_flash_params[] = {
    {0xA1, 0x40, 0x13, 512 * 1024, 4096, 256, "FM25Q04B"},
};

const spiflash_params_t* m_flash_info = NULL;

const spiflash_params_t* spiflash_params_get(void) {
    return m_flash_info;
}

#define SPI_INSTANCE 1
static const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);

static uint8_t spi_buffer[260];

void SPIFlash_Init(void) {
    nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
    spi_config.ss_pin    = FLASH_CS_PIN;
    spi_config.miso_pin  = FLASH_DO_PIN;
    spi_config.mosi_pin  = FLASH_DI_PIN;
    spi_config.sck_pin   = FLASH_CLK_PIN;
    spi_config.frequency = NRF_DRV_SPI_FREQ_4M;
#if defined(S112)
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, NULL, NULL));
#else
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, NULL));
#endif
    SPIFlash_Reset();
}

static void SPIFlash_WriteRead(uint8_t* tx_buf, uint16_t tx_len, uint8_t* rx_buf, uint16_t rx_len) {
    const nrfx_spi_xfer_desc_t spi_xfer_desc = {
        .p_tx_buffer = tx_buf,
        .tx_length   = tx_len,
        .p_rx_buffer = rx_buf,
        .rx_length   = rx_len,
    };
    APP_ERROR_CHECK(nrfx_spi_xfer(&spi.u.spi, &spi_xfer_desc, 0));
}

bool SPIFlash_Detect(void) {
    uint8_t cmd = SPIFlash_CMD_RDID;
    SPIFlash_WriteRead(&cmd, 1, spi_buffer, 4);

    for (int i = 0; i < ARRAY_SIZE(m_flash_params); i++) {
        if (m_flash_params[i].manufacturer_id == spi_buffer[1] &&
            m_flash_params[i].memory_type == spi_buffer[2] &&
            m_flash_params[i].capacity == spi_buffer[3]) {
            m_flash_info = &m_flash_params[i];
            NRF_LOG_INFO("SPI Flash detected: %s", m_flash_params[i].name);
            return true;
        }
    }

    NRF_LOG_ERROR("SPI Flash not supported or not found!");
    m_flash_info = NULL;
    return false;
}

void SPIFlash_Reset(void) {
    uint8_t cmd[2];
    cmd[0] = SPIFlash_CMD_ENRST;
    cmd[1] = SPIFlash_CMD_RST;
    SPIFlash_WriteRead(cmd, 2, NULL, 0);
    nrf_delay_us(50);
}

static bool SPIFlash_Busy(void) {
    uint8_t cmd = SPIFlash_CMD_RDSR1;
    uint8_t status[2];
    SPIFlash_WriteRead(&cmd, 1, status, 2);
    return (status[1] & 0x01) != 0;
}

static void SPIFlash_EnableWrite(void) {
    uint8_t cmd = SPIFlash_CMD_WREN;
    SPIFlash_WriteRead(&cmd, 1, NULL, 0);
}

static void SPIFlash_DisableWrite(void) {
    uint8_t cmd = SPIFlash_CMD_WRDI;
    SPIFlash_WriteRead(&cmd, 1, NULL, 0);
}

void SPIFlash_EraseSector(uint32_t addr) {
    uint8_t cmd[4];
    cmd[0] = SPIFlash_CMD_ERASE4K;
    cmd[1] = (addr >> 16) & 0xFF;  // MSB of the memory address
    cmd[2] = (addr >>  8) & 0xFF;
    cmd[3] = (addr) & 0xFF;        // LSB of the memory address

    SPIFlash_EnableWrite();
    SPIFlash_WriteRead(cmd, 4, NULL, 0);
    while (SPIFlash_Busy()) {
        nrf_delay_ms(10);
    }
}

void SPIFlash_EraseChip(void) {
    uint8_t cmd = SPIFlash_CMD_CHIP_ERASE;
    SPIFlash_EnableWrite();
    SPIFlash_WriteRead(&cmd, 1,  NULL, 0);
    while (SPIFlash_Busy()) {
        nrf_delay_ms(1000);
    }
}

void SPIFlash_WritePage(uint32_t addr, const uint8_t* buffer, uint16_t len) {
    spi_buffer[0] = SPIFlash_CMD_PROG;
    spi_buffer[1] = (addr >> 16) & 0xFF;  // MSB of the memory address
    spi_buffer[2] = (addr >>  8) & 0xFF;
    spi_buffer[3] = (addr) & 0xFF;        // LSB of the memory address

    SPIFlash_EnableWrite();
    memcpy(spi_buffer + 4, buffer, len);
    SPIFlash_WriteRead(spi_buffer, 4 + len, NULL, 0);
    while (SPIFlash_Busy()) {
        nrf_delay_ms(1);
    }
}

void SPIFlash_Write(uint32_t addr, const uint8_t* buffer, uint32_t len) {
    uint32_t page_size = m_flash_info->page_size;

    uint32_t page_offset = addr % page_size;
    if (page_offset != 0) {
        uint32_t partial_len = MIN(page_size - page_offset, len);
        SPIFlash_WritePage(addr, buffer, partial_len);
        addr += partial_len;
        buffer += partial_len;
        len -= partial_len;
    }

    while (len >= page_size) {
        SPIFlash_WritePage(addr, buffer, page_size);
        addr += page_size;
        buffer += page_size;
        len -= page_size;
    }

    if (len > 0) {
        SPIFlash_WritePage(addr, buffer, len);
    }
}

void SPIFlash_Read(uint32_t addr, uint8_t* buffer, uint16_t len) {
    uint8_t cmd[4];
    cmd[0] = SPIFlash_CMD_READ;
    cmd[1] = (addr >> 16) & 0xFF;   // MSB of the memory address
    cmd[2] = (addr >>  8) & 0xFF;
    cmd[3] = (addr) & 0xFF;         // LSB of the memory address

    SPIFlash_WriteRead(cmd, 4, spi_buffer, 4 + len);
    memcpy(buffer, spi_buffer + 4, len);
}
#endif
