#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>

// <h> APP_Drivers

// <q> GPIO_WAKEUP_ENABLED - gpiote - GPIO external wakeup
// <i> GPIO sense wakeup for boards with an external wake source.
// <i> need set GPIOTE_ENABLED to 1 in SDK sdk_config.h.
#define GPIO_WAKEUP_ENABLED GPIOTE_ENABLED

// <h> EPD_driver - EPD driver

// <q> EPD_UC8176_420_BW_ENABLED - UC8176 4.2" BW
#define EPD_UC8176_420_BW_ENABLED 1

// <q> EPD_UC8176_420_BWR_ENABLED - UC8176 4.2" BWR
#define EPD_UC8176_420_BWR_ENABLED 1

// <q> EPD_UC8159_750_BW_ENABLED - UC8159 7.5" BW
#define EPD_UC8159_750_BW_ENABLED 1

// <q> EPD_UC8159_750_BWR_ENABLED - UC8159 7.5" BWR
#define EPD_UC8159_750_BWR_ENABLED 1

// <q> EPD_UC8179_750_BW_ENABLED - UC8179 7.5" BW
#define EPD_UC8179_750_BW_ENABLED 1

// <q> EPD_UC8179_750_BWR_ENABLED - UC8179 7.5" BWR
#define EPD_UC8179_750_BWR_ENABLED 1

// <q> EPD_UC8159_583_BW_ENABLED - UC8159 5.83" BW
#define EPD_UC8159_583_BW_ENABLED 1

// <q> EPD_UC8159_583_BWR_ENABLED - UC8159 5.83" BWR
#define EPD_UC8159_583_BWR_ENABLED 1

// <q> EPD_UC8179_583_BW_ENABLED - UC8179 5.83" BW
#define EPD_UC8179_583_BW_ENABLED 1

// <q> EPD_UC8179_583_BWR_ENABLED - UC8179 5.83" BWR
#define EPD_UC8179_583_BWR_ENABLED 1

// <q> EPD_SSD1619_420_BWR_ENABLED - SSD1619 4.2" BWR
#define EPD_SSD1619_420_BWR_ENABLED 1

// <q> EPD_SSD1619_420_BW_ENABLED - SSD1619 4.2" BW
#define EPD_SSD1619_420_BW_ENABLED 1

// <q> EPD_SSD1677_750_BWR_ENABLED - SSD1677 7.5" BWR
#define EPD_SSD1677_750_BWR_ENABLED 1

// <q> EPD_SSD1677_750_BW_ENABLED - SSD1677 7.5" BW
#define EPD_SSD1677_750_BW_ENABLED 1

// <q> EPD_JD79668_420_BWRY_ENABLED - JD79668 4.2" BWRY
#define EPD_JD79668_420_BWRY_ENABLED 1

// <q> EPD_JD79665_750_BWRY_ENABLED - JD79665 7.5" BWRY
#define EPD_JD79665_750_BWRY_ENABLED 1

// <q> EPD_JD79665_583_BWRY_ENABLED - JD79665 5.83" BWRY
#define EPD_JD79665_583_BWRY_ENABLED 1

// </h>

// <e> SPIFLASH_ENABLED - flash - SPI Flash driver
#define SPIFLASH_ENABLED 0

// <o> FLASH_CS_PIN - SPI Flash CS pin
#define FLASH_CS_PIN 12

// <o> FLASH_DO_PIN - SPI Flash DO pin
#define FLASH_DO_PIN 13

// <o> FLASH_DI_PIN - SPI Flash DI pin
#define FLASH_DI_PIN 15

// <o> FLASH_CLK_PIN - SPI Flash CLK pin
#define FLASH_CLK_PIN 14

// </e>

// </h>

// <h> APP_FileSystem

// <e> FILESYSTEM_ENABLED - filesystem - LittleFS file system
#define FILESYSTEM_ENABLED 0

// <o> FILESYSTEM_BECKEND - File system backend
// <i> FILESYSTEM_BECKEND_FSTORAGE uses the nrf_fstorage backend implementation to write to on-chip flash.
// <i> FILESYSTEM_BECKEND_EXTERNAL_FLASH uses the SPI flash driver to write to external flash. Use this setting if you have an external flash.
// <1=> FILESYSTEM_BECKEND_FSTORAGE
// <2=> FILESYSTEM_BECKEND_EXTERNAL_FLASH
#define FILESYSTEM_BECKEND 2

// <s> IMAGE_PATH - Image directory in file system
#define IMAGE_PATH "/img"

// </e>

#if defined(NRF51822)
#ifdef __GNUC__
#define APP_STACK_SIZE 2048
#define APP_HEAP_SIZE 1024
#else
#define APP_STACK_SIZE 1200
#define APP_HEAP_SIZE 4000
#endif
#elif defined(NRF52811_XXAA)
#if FILESYSTEM_ENABLED
#define APP_STACK_SIZE 4096
#define APP_HEAP_SIZE 8192
#else
#define APP_STACK_SIZE 2048
#define APP_HEAP_SIZE 8192
#endif
#endif

#if FILESYSTEM_ENABLED
#define GUI_HEAP_RESERVE 2048
#else
#define GUI_HEAP_RESERVE 512
#endif

// </h>

#endif
