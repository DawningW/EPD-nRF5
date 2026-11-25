#include "filesystem.h"
#include "nrf_log.h"
#include "flash.h"
#include "app_config.h"

#if FILESYSTEM_ENABLED
static lfs_t lfs;
static struct lfs_config cfg;

static int lfs_read(const struct lfs_config* c, lfs_block_t block,
                    lfs_off_t off, void* buffer, lfs_size_t size) {
    SPIFlash_Read(block * c->block_size + off, (uint8_t*)buffer, size);
    return 0;
}

static int lfs_prog(const struct lfs_config* c, lfs_block_t block,
                    lfs_off_t off, const void* buffer, lfs_size_t size) {
    SPIFlash_Write(block * c->block_size + off, (const uint8_t*)buffer, size);
    return 0;
}

static int lfs_erase(const struct lfs_config* c, lfs_block_t block) {
    SPIFlash_EraseSector(block * c->block_size);
    return 0;
}

static int lfs_sync(const struct lfs_config* c) {
    return 0;
}

void FS_Init(bool auto_format) {
    SPIFlash_Init();
    if (!SPIFlash_Detect()) {
        return;
    }

    const spiflash_params_t* flash_info = spiflash_params_get();
    // block device operations
    cfg.read = lfs_read;
    cfg.prog = lfs_prog;
    cfg.erase = lfs_erase;
    cfg.sync = lfs_sync;
    // block device configuration
    cfg.read_size = 1;
    cfg.prog_size = flash_info->page_size; // some flash only support page programming
    cfg.block_size = flash_info->sector_size;
    cfg.block_count = flash_info->total_size / flash_info->sector_size;
    cfg.cache_size = flash_info->page_size;
    cfg.lookahead_size = 16;
    cfg.block_cycles = 500;

    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        NRF_LOG_ERROR("LittleFS mount failed with %d", err);
        if (!auto_format) {
            return;
        }
        NRF_LOG_INFO("auto format is enabled, formatting...");
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
        if (err) {
            NRF_LOG_ERROR("LittleFS mount failed again with %d", err);
            return;
        }
        FS_Mkdir(IMAGE_PATH);
    }
    NRF_LOG_INFO("LittleFS mounted");
}

void FS_Deinit(void) {
    lfs_unmount(&lfs);
    NRF_LOG_INFO("LittleFS unmounted");
}

void FS_Format(void) {
    lfs_unmount(&lfs);
    lfs_format(&lfs, &cfg);
    lfs_mount(&lfs, &cfg);
    FS_Mkdir(IMAGE_PATH);
}

void FS_Stat(FSInfo* info) {
    struct lfs_fsinfo fsinfo;
    if (lfs_fs_stat(&lfs, &fsinfo) < 0) {
        memset(info, 0, sizeof(FSInfo));
        return;
    }
    lfs_ssize_t used_block = lfs_fs_size(&lfs);
    info->total_size = fsinfo.block_count * fsinfo.block_size;
    info->used_size = used_block * fsinfo.block_size;
}

int FS_Open(File* file, const char* path, int flags) {
    return lfs_file_open(&lfs, file, path, flags);
}

int FS_Close(File* file) {
    return lfs_file_close(&lfs, file);
}

int FS_Read(File* file, void* buffer, uint32_t len) {
    return lfs_file_read(&lfs, file, buffer, len);
}

int FS_Write(File* file, const void* buffer, uint32_t len) {
    return lfs_file_write(&lfs, file, buffer, len);
}

int FS_Seek(File* file, int offset, int whence) {
    return lfs_file_seek(&lfs, file, offset, whence);
}

int FS_Tell(File* file) {
    return lfs_file_tell(&lfs, file);
}

int FS_Mkdir(const char* path) {
    return lfs_mkdir(&lfs, path);
}

int FS_Remove(const char* path) {
    return lfs_remove(&lfs, path);
}

int FS_Rename(const char* old_path, const char* new_path) {
    return lfs_rename(&lfs, old_path, new_path);
}

int FS_OpenDir(Directory* dir, const char* path) {
    return lfs_dir_open(&lfs, dir, path);
}

int FS_CloseDir(Directory* dir) {
    return lfs_dir_close(&lfs, dir);
}

int FS_ReadDir(Directory* dir, FileInfo* info) {
    return lfs_dir_read(&lfs, dir, info);
}
#endif
