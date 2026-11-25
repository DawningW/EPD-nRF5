#ifndef __FLASH_H
#define __FLASH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "app_config.h"

#if SPIFLASH_ENABLED
typedef struct {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
    uint32_t total_size;
    uint32_t sector_size;
    uint32_t page_size;
    const char* name;
} spiflash_params_t;

const spiflash_params_t* spiflash_params_get(void);

// SPI Flash commands
enum {
    SPIFlash_CMD_WREN       = 0x06,    // Write Enable
    SPIFlash_CMD_WRDI       = 0x04,    // Write Disable
    SPIFlash_CMD_RDSR1      = 0x05,    // Read Status Register-1
    SPIFlash_CMD_RDSR2      = 0x35,    // Read Status Register-2
    SPIFlash_CMD_WRSR       = 0x01,    // Write Status Register
    SPIFlash_CMD_READ       = 0x03,    // Read Data
    SPIFlash_CMD_FAST_READ  = 0x0B,    // Fast Read
    SPIFlash_CMD_PROG       = 0x02,    // Page Program
    SPIFlash_CMD_ERASE4K    = 0x20,    // Sector Erase (4KB)
    SPIFlash_CMD_ERASE32K   = 0x52,    // Block Erase (32KB)
    SPIFlash_CMD_ERASE64K   = 0xD8,    // Block Erase (64KB)
    SPIFlash_CMD_CHIP_ERASE = 0xC7,    // Chip Erase
    SPIFlash_CMD_PD         = 0xB9,    // Power-down
    SPIFlash_CMD_REL_PD     = 0xAB,    // Release Power-down / Device ID
    SPIFlash_CMD_MF_ID      = 0x90,    // Manufacturer / Device ID
    SPIFlash_CMD_RDID       = 0x9F,    // JEDEC ID
    SPIFlash_CMD_ENRST      = 0x66,    // Enable Reset
    SPIFlash_CMD_RST        = 0x99     // Reset Device
};

// SPI Flash
void SPIFlash_Init(void);
bool SPIFlash_Detect(void);
void SPIFlash_Reset(void);
void SPIFlash_EraseSector(uint32_t addr);
void SPIFlash_EraseChip(void);
void SPIFlash_WritePage(uint32_t addr, const uint8_t* buffer, uint16_t len);
void SPIFlash_Write(uint32_t addr, const uint8_t* buffer, uint32_t len);
void SPIFlash_Read(uint32_t addr, uint8_t* buffer, uint16_t len);
#endif

#endif
