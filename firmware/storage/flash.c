#include "board.h"
/* F1C200s SPI0/NOR, polled with deadlines */
#include "storage.h"
#include <stddef.h>
#define SPI0_BASE 0x01c05000u
extern void scope_boot_cursor_poll(void);
static unsigned flash_initialized, flash_jedec_id, sequence;
static unsigned reading_boot_assets;
extern void usb_poll(void);
static uint32_t flash_timer_ticks(void) {
    return MMIO32(0x01c20c28u);
}
static int flash_timeout_elapsed(uint32_t started_ticks, unsigned duration_ms) {
    return started_ticks - flash_timer_ticks() > duration_ms * 24000u;
}
static int flash_spi_transfer(uint8_t *data, unsigned byte_count) {
    if (!flash_initialized || !byte_count || byte_count > 64) {
        return 0;
    }
    MMIO32(SPI0_BASE + 0x18) = 0x80008000u;
    MMIO32(SPI0_BASE + 0x30) = byte_count;
    MMIO32(SPI0_BASE + 0x34) = byte_count;
    MMIO32(SPI0_BASE + 0x38) = byte_count;
    MMIO32(SPI0_BASE + 8) = 0x44u;
    for (unsigned i = 0; i < byte_count; i++) {
        MMIO8(SPI0_BASE + 0x200) = data[i];
    }
    MMIO32(SPI0_BASE + 8) |= 0x80000000u;
    uint32_t started_ticks = flash_timer_ticks();
    while ((MMIO32(SPI0_BASE + 0x1c) & 0x7f) < byte_count) {
        if (flash_timeout_elapsed(started_ticks, 100)) {
            MMIO32(SPI0_BASE + 8) = 0xc4;
            flash_initialized = 0;
            return 0;
        }
    }
    for (unsigned i = 0; i < byte_count; i++) {
        data[i] = MMIO8(SPI0_BASE + 0x300);
    }
    while (MMIO32(SPI0_BASE + 8) & 0x80000000u) {
        if (flash_timeout_elapsed(started_ticks, 100)) {
            MMIO32(SPI0_BASE + 8) = 0xc4;
            flash_initialized = 0;
            return 0;
        }
    }
    MMIO32(SPI0_BASE + 8) = 0xc4;
    return 1;
}
int scope_flash_init(void) {
    if (flash_initialized) {
        return 1;
    }
    MMIO32(0x01c20848u) = (MMIO32(0x01c20848u) & ~0xffffu) | 0x2222u;
    MMIO32(0x01c202c0u) |= 1u << 20;
    MMIO32(0x01c20060u) |= 1u << 20;
    MMIO32(SPI0_BASE + 4) = 0x80000003u;
    uint32_t started_ticks = flash_timer_ticks();
    while (MMIO32(SPI0_BASE + 4) & 0x80000000u) {
        if (flash_timeout_elapsed(started_ticks, 100)) {
            return 0;
        }
    }
    /* Leave CPU/AHB/DDR clocks alone; conservatively divide current AHB by 32. */
    MMIO32(SPI0_BASE + 0x24) = 0x100fu;
    MMIO32(SPI0_BASE + 8) = 0xc4u;
    flash_initialized = 1;
    uint8_t identify_command[4] = {0x9f, 0, 0, 0};
    if (!flash_spi_transfer(identify_command, 4)) {
        return 0;
    }
    flash_jedec_id = ((unsigned)identify_command[1] << 16) | ((unsigned)identify_command[2] << 8) |
                     identify_command[3];
    if (flash_jedec_id != 0xc84018u) {
        flash_initialized = 0;
        return 0;
    }
    return 1;
}
unsigned scope_flash_id(void) {
    return flash_jedec_id;
}
void scope_flash_release(void) {
    if (MMIO32(0x01c20060u) & (1u << 20)) {
        MMIO32(SPI0_BASE + 8) = 0x84u;
    }
    flash_initialized = 0;
}
int scope_flash_read(uint32_t address, void *destination_buffer, unsigned remaining_bytes) {
    if (address > 0x1000000u || remaining_bytes > 0x1000000u - address || !scope_flash_init()) {
        return 0;
    }
    uint8_t *destination_cursor = destination_buffer;
    while (remaining_bytes) {
        scope_boot_cursor_poll();
        if (reading_boot_assets) {
            usb_poll();
        }
        unsigned count = remaining_bytes > 60 ? 60 : remaining_bytes;
        uint8_t data[64];
        data[0] = 3;
        data[1] = address >> 16;
        data[2] = address >> 8;
        data[3] = address;
        for (unsigned i = 0; i < count; i++) {
            data[4 + i] = 0xff;
        }
        if (!flash_spi_transfer(data, count + 4)) {
            return 0;
        }
        for (unsigned i = 0; i < count; i++) {
            *destination_cursor++ = data[i + 4];
        }
        address += count;
        remaining_bytes -= count;
    }
    return 1;
}
static uint32_t crc32_accumulate(uint32_t crc, const void *source, unsigned byte_count) {
    const uint8_t *source_bytes = source;
    while (byte_count--) {
        if (!(byte_count & 4095)) {
            scope_boot_cursor_poll();
            if (reading_boot_assets) {
                usb_poll();
            }
        }
        crc ^= *source_bytes++;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
        }
    }
    return crc;
}
uint32_t scope_crc32(const void *source, unsigned byte_count) {
    return ~crc32_accumulate(0xffffffffu, source, byte_count);
}
extern void scope_boot_log(const char *);
static int flash_read_boot_assets(void) {
    uint32_t header[2];
    MMIO32(0x800a0104u) = 0;
    if (MMIO32(0x800a0108u) != 0x4c4f474fu) {
        if (!scope_flash_read(0xc000u, (void *)0x81b00000u, 768000u)) {
            return 0;
        }
        MMIO32(0x800a0108u) = 0x4c4f474fu;
    }
    scope_boot_log("READING FPGA IMAGE");
    if (!scope_flash_read(0x600000u, header, 8) || header[0] != 636686u) {
        return 0;
    }
    if (!scope_flash_read(0x600008u, (void *)0x817230c0u, 636678u)) {
        return 0;
    }
    scope_boot_log("VERIFYING FPGA IMAGE");
    if (scope_crc32((const void *)0x817230c0u, 636678u) != 0xecb2dd0cu) {
        return 0;
    }
    MMIO32(0x800a0104u) = 0x46504741u;
    return 1;
}
int scope_assets_from_flash(void) {
    reading_boot_assets = 1;
    int result = flash_read_boot_assets();
    reading_boot_assets = 0;
    return result;
}
#define SETTINGS_RECORD_MAGIC 0x32535348u
#define SETTINGS_COMMIT_MARKER 0x54494d43u
struct settings_journal_record {
    uint32_t magic, version, length, sequence, crc, commit;
    struct scope_saved_settings payload;
};
_Static_assert(sizeof(struct settings_journal_record) <= 4096,
               "Settings record exceeds one erase sector");
static struct settings_journal_record settings_slots[2], pending_settings_record;
static unsigned settings_slot_valid[2];
static uint32_t settings_record_crc(const struct settings_journal_record *journal_record) {
    uint32_t crc = crc32_accumulate(0xffffffffu, journal_record, 16);
    return ~crc32_accumulate(crc, &journal_record->payload, journal_record->length);
}
static int settings_read_slots(void) {
    for (unsigned i = 0; i < 2; i++) {
        if (!scope_flash_read(i ? SCOPE_SETTINGS_SLOT1 : SCOPE_SETTINGS_SLOT0,
                              &settings_slots[i],
                              sizeof(struct settings_journal_record))) {
            return 0;
        }
        struct settings_journal_record *journal_record = &settings_slots[i];
        unsigned legacy = (journal_record->version == 2 || journal_record->version == 3) &&
                          journal_record->length == offsetof(struct scope_saved_settings, advanced);
        unsigned has_features_v4 =
            journal_record->version == 4 &&
            journal_record->length == offsetof(struct scope_saved_settings, features);
        unsigned has_adc_gain_v5 =
            journal_record->version == 5 &&
            journal_record->length == offsetof(struct scope_saved_settings, adc_gain);
        unsigned has_baseline_v6 =
            journal_record->version == 6 &&
            journal_record->length == offsetof(struct scope_saved_settings, voltage_baseline);
        unsigned current = (journal_record->version == 7 || journal_record->version == 8) &&
                           journal_record->length == sizeof(journal_record->payload);
        settings_slot_valid[i] =
            journal_record->magic == SETTINGS_RECORD_MAGIC &&
            (legacy || has_features_v4 || has_adc_gain_v5 || has_baseline_v6 || current) &&
            journal_record->commit == SETTINGS_COMMIT_MARKER &&
            journal_record->crc == settings_record_crc(journal_record);
        if (settings_slot_valid[i] && legacy) {
            scope_advanced_defaults(&journal_record->payload.advanced);
            scope_acquisition_defaults(&journal_record->payload.acquisition);
        }
        if (settings_slot_valid[i] && (legacy || has_features_v4)) {
            scope_features_defaults(&journal_record->payload.features);
        }
        if (settings_slot_valid[i] && !current && !has_baseline_v6) {
            scope_adc_gain_defaults(journal_record->payload.adc_gain);
        }
        if (settings_slot_valid[i] && !current) {
            journal_record->payload.voltage_baseline = 0;
        }
        if (settings_slot_valid[i]) {
            settings_slot_valid[i] = scope_saved_settings_valid(&journal_record->payload);
        }
    }
    return 1;
}
static unsigned settings_latest_slot(void) {
    return settings_slot_valid[1] &&
                   (!settings_slot_valid[0] ||
                    (int32_t)(settings_slots[1].sequence - settings_slots[0].sequence) > 0)
               ? 1
               : 0;
}
int scope_settings_load(struct scope_saved_settings *settings_result) {
    if (!settings_read_slots()) {
        return -1;
    }
    if (!settings_slot_valid[0] && !settings_slot_valid[1]) {
        return 0;
    }
    unsigned slot_index = settings_latest_slot();
    *settings_result = settings_slots[slot_index].payload;
    sequence = settings_slots[slot_index].sequence;
    return 1;
}
unsigned scope_settings_sequence(void) {
    return sequence;
}
static int flash_read_status(unsigned *value) {
    uint8_t data[2] = {5, 0};
    if (!flash_spi_transfer(data, 2)) {
        return 0;
    }
    *value = data[1];
    return 1;
}
static int flash_wait_ready(unsigned duration_ms) {
    uint32_t start = flash_timer_ticks();
    unsigned status_register;
    do {
        if (!flash_read_status(&status_register)) {
            return 0;
        }
        if (!(status_register & 1)) {
            return 1;
        }
    } while (!flash_timeout_elapsed(start, duration_ms));
    return 0;
}
static int flash_write_enable(void) {
    uint8_t write_enable_command = 6;
    unsigned status_register;
    return flash_spi_transfer(&write_enable_command, 1) && flash_read_status(&status_register) &&
           (status_register & 2);
}
static int flash_program_bytes(uint32_t address, const void *source, unsigned remaining_bytes) {
    const uint8_t *source_bytes = source;
    while (remaining_bytes) {
        unsigned count = 256 - (address & 255);
        if (count > 60) {
            count = 60;
        }
        if (count > remaining_bytes) {
            count = remaining_bytes;
        }
        uint8_t data[64] = {2, address >> 16, address >> 8, address};
        for (unsigned i = 0; i < count; i++) {
            data[4 + i] = source_bytes[i];
        }
        if (!flash_write_enable() || !flash_spi_transfer(data, count + 4) ||
            !flash_wait_ready(20)) {
            return 0;
        }
        address += count;
        source_bytes += count;
        remaining_bytes -= count;
    }
    return 1;
}
static int settings_slot_owned_or_erased(uint32_t address) {
    uint8_t data[60];
    uint32_t magic;
    if (!scope_flash_read(address, &magic, 4)) {
        return 0;
    }
    if (magic == SETTINGS_RECORD_MAGIC) {
        return 1;
    }
    for (unsigned byte_offset = 0; byte_offset < 4096;) {
        unsigned chunk_bytes = 4096 - byte_offset;
        if (chunk_bytes > 60) {
            chunk_bytes = 60;
        }
        if (!scope_flash_read(address + byte_offset, data, chunk_bytes)) {
            return 0;
        }
        for (unsigned i = 0; i < chunk_bytes; i++) {
            if (data[i] != 0xff) {
                return 0;
            }
        }
        byte_offset += chunk_bytes;
    }
    return 1;
}
int scope_settings_save(const struct scope_saved_settings *settings_to_save) {
    if (SCOPE_VOLATILE_SETTINGS) {
        (void)settings_to_save;
        return 0;
    }
    if (!settings_read_slots()) {
        return 0;
    }
    unsigned newest = settings_latest_slot(),
             target = (settings_slot_valid[0] || settings_slot_valid[1]) ? newest ^ 1 : 0;
    if (settings_slot_valid[0] || settings_slot_valid[1]) {
        const uint8_t *expected_bytes = (const uint8_t *)settings_to_save,
                      *actual_bytes = (const uint8_t *)&settings_slots[newest].payload;
        unsigned payload_matches = 1;
        for (unsigned i = 0; i < sizeof(*settings_to_save); i++) {
            if (expected_bytes[i] != actual_bytes[i]) {
                payload_matches = 0;
                break;
            }
        }
        if (payload_matches && settings_slots[newest].version == 8) {
            sequence = settings_slots[newest].sequence;
            return 1;
        }
    }
    uint32_t address = target ? SCOPE_SETTINGS_SLOT1 : SCOPE_SETTINGS_SLOT0;
    if (!settings_slot_owned_or_erased(address)) {
        return 0;
    }
    pending_settings_record.magic = SETTINGS_RECORD_MAGIC;
    pending_settings_record.version = 8;
    pending_settings_record.length = sizeof(pending_settings_record.payload);
    pending_settings_record.sequence = (settings_slot_valid[0] || settings_slot_valid[1])
                                           ? settings_slots[newest].sequence + 1
                                           : 1;
    pending_settings_record.payload = *settings_to_save;
    pending_settings_record.commit = 0xffffffffu;
    pending_settings_record.crc = settings_record_crc(&pending_settings_record);
    uint8_t erase[4] = {0x20, address >> 16, address >> 8, address};
    if (!flash_write_enable() || !flash_spi_transfer(erase, 4) || !flash_wait_ready(1000) ||
        !flash_program_bytes(address, &pending_settings_record, sizeof(pending_settings_record))) {
        return 0;
    }
    struct settings_journal_record readback;
    if (!scope_flash_read(address, &readback, sizeof(readback))) {
        return 0;
    }
    const uint8_t *expected_bytes = (const uint8_t *)&pending_settings_record,
                  *actual_bytes = (const uint8_t *)&readback;
    for (unsigned i = 0; i < sizeof(pending_settings_record); i++) {
        if (expected_bytes[i] != actual_bytes[i]) {
            return 0;
        }
    }
    uint32_t commit = SETTINGS_COMMIT_MARKER;
    if (!flash_program_bytes(
            address + offsetof(struct settings_journal_record, commit), &commit, 4) ||
        !settings_read_slots() || !settings_slot_valid[target]) {
        return 0;
    }
    sequence = pending_settings_record.sequence;
    return 1;
}
