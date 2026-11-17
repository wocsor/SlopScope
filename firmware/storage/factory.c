#include "factory.h"
#include "factory_adc.h"
#include "lfs.h"
#include "storage.h"
#define FACTORY_RECORD_BYTES 0x8650u
static lfs_t factory_filesystem;
static lfs_file_t factory_record_file;
static uint8_t read_cache[256], program_cache[256], lookahead[16], file_cache[256];
static uint8_t factory_record[FACTORY_RECORD_BYTES];
static unsigned factory_load_state, factory_record_crc;
extern void usb_poll(void);
static uint32_t factory_read_u32_le(unsigned byte_offset) {
    return (uint32_t)factory_record[byte_offset] | (uint32_t)factory_record[byte_offset + 1] << 8 |
           (uint32_t)factory_record[byte_offset + 2] << 16 |
           (uint32_t)factory_record[byte_offset + 3] << 24;
}
static int factory_read_block(const struct lfs_config *filesystem_config,
                              lfs_block_t block,
                              lfs_off_t byte_offset,
                              void *destination,
                              lfs_size_t byte_count) {
    (void)filesystem_config;
    if (block >= 2304 || byte_offset > 4096 || byte_count > 4096 - byte_offset) {
        return LFS_ERR_IO;
    }
    /* Only the boot-time factory loader uses these LittleFS callbacks. */
    usb_poll();
    return scope_flash_read(0x700000u + block * 4096 + byte_offset, destination, byte_count)
               ? 0
               : LFS_ERR_IO;
}
static const struct lfs_config factory_filesystem_config = {.read = factory_read_block,
                                                            .read_size = 16,
                                                            .prog_size = 16,
                                                            .block_size = 4096,
                                                            .block_count = 2304,
                                                            .block_cycles = -1,
                                                            .cache_size = 256,
                                                            .lookahead_size = 16,
                                                            .read_buffer = read_cache,
                                                            .prog_buffer = program_cache,
                                                            .lookahead_buffer = lookahead};
static const struct lfs_file_config factory_file_config = {.buffer = file_cache};
static int factory_record_valid(void) {
    if (factory_read_u32_le(0x6c) != 0x069194c5u) {
        return 0;
    }
    uint32_t checksum = 0;
    for (unsigned byte_index = 0; byte_index < 0x8640; byte_index++) {
        if (!(byte_index & 4095)) {
            usb_poll();
        }
        checksum ^= (uint32_t)((int32_t)(int8_t)factory_record[byte_index] * 256);
        for (unsigned i = 0; i < 8; i++) {
            checksum = (checksum << 1) ^ ((checksum & 0x8000) ? 0x1021 : 0);
        }
    }
    if (checksum != factory_read_u32_le(0x8644)) {
        return 0;
    }
    if (factory_read_u32_le(0x804) > 7 || factory_read_u32_le(0x808) > 7) {
        return 0;
    }
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        for (unsigned range_index = 0; range_index < 14; range_index++) {
            if (factory_read_u32_le((channel_index ? 0x134 : 0xfc) + range_index * 4) > 65535 ||
                factory_read_u32_le((channel_index ? 0x1a4 : 0x16c) + range_index * 4) > 65535 ||
                factory_read_u32_le((channel_index ? 0x254 : 0x1e4) + range_index * 4) > 65535) {
                return 0;
            }
        }
    }
    return 1;
}
int scope_factory_load(void) {
    factory_load_state = 0;
    factory_record_crc = 0;
    if (lfs_mount(&factory_filesystem, &factory_filesystem_config) < 0) {
        factory_load_state = 3;
        return 0;
    }
    for (unsigned i = 0; i < 2; i++) {
        if (lfs_file_opencfg(&factory_filesystem,
                             &factory_record_file,
                             i ? "table1" : "table0",
                             LFS_O_RDONLY,
                             &factory_file_config) < 0) {
            continue;
        }
        int record_valid =
            lfs_file_size(&factory_filesystem, &factory_record_file) == FACTORY_RECORD_BYTES;
        if (record_valid) {
            record_valid = lfs_file_read(&factory_filesystem,
                                         &factory_record_file,
                                         factory_record,
                                         FACTORY_RECORD_BYTES) == FACTORY_RECORD_BYTES;
        }
        lfs_file_close(&factory_filesystem, &factory_record_file);
        if (record_valid && factory_record_valid()) {
            factory_load_state = i + 1;
            factory_record_crc = scope_crc32(factory_record, FACTORY_RECORD_BYTES);
            break;
        }
    }
    lfs_unmount(&factory_filesystem);
    if (!factory_load_state) {
        factory_load_state = 4;
    }
    return factory_load_state <= 2;
}
unsigned scope_factory_status(void) {
    return factory_load_state;
}
unsigned scope_factory_crc(void) {
    return factory_record_crc;
}
const uint8_t *scope_factory_table(unsigned *length) {
    if (!length) {
        return 0;
    }
    *length = factory_load_state >= 1 && factory_load_state <= 2 ? FACTORY_RECORD_BYTES : 0;
    return *length ? factory_record : 0;
}
unsigned scope_factory_phase(unsigned channel_index) {
    return factory_load_state >= 1 && factory_load_state <= 2 && channel_index < 2
               ? factory_read_u32_le(0x804 + channel_index * 4)
           : channel_index ? 7
                           : 6;
}
int scope_factory_position(unsigned channel_index, unsigned range, int *zero, int *slope) {
    if (factory_load_state < 1 || factory_load_state > 2 || channel_index > 1 || range >= 14) {
        return 0;
    }
    *zero = (int)factory_read_u32_le((channel_index ? 0x134 : 0xfc) + range * 4);
    *slope = (int)factory_read_u32_le((channel_index ? 0x1a4 : 0x16c) + range * 4);
    return 1;
}
unsigned scope_factory_adc(unsigned channel_index, unsigned range, unsigned fallback) {
    if (factory_load_state < 1 || factory_load_state > 2 || channel_index > 1 || range >= 14) {
        return fallback;
    }
    if (channel_index > 1 || range >= 14) {
        return fallback;
    }
    unsigned coefficient = factory_read_u32_le((channel_index ? 0x254 : 0x1e4) + range * 4);
    if (coefficient > 128) {
        return fallback;
    }
    /* Stock 0x7f600 complements the saved service coefficient before lookup. */
    unsigned index = 128 - coefficient;
    if (index > 127) {
        index = 127;
    }
    return factory_adc_scale[index];
}

unsigned scope_factory_adc_trim(unsigned channel_index, unsigned range, unsigned fallback) {
    if (factory_load_state < 1 || factory_load_state > 2 || channel_index > 1 || range >= 14) {
        return fallback;
    }
    if (channel_index > 1 || range >= 14) {
        return fallback;
    }
    unsigned coefficient = factory_read_u32_le((channel_index ? 0x254 : 0x1e4) + range * 4);
    if (coefficient > 128) {
        return fallback;
    }
    unsigned index = 128 - coefficient;
    if (index > 127) {
        index = 127;
    }
    return factory_adc_trim[index];
}
