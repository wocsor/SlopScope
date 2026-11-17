#include "board.h"
/* SRAM-resident DRAM and screen bring-up */
#include <stddef.h>
#include <stdint.h>
#define DRAM_TEST_BASE 0x80000000u
#define DRAM_TEST_BYTES 0x02000000u
#define DISPLAY_TEST_FRAMEBUFFER 0x81c00000u
#define DRAM_RESULT_MAGIC 0x4252494eu
struct dram_diagnostic_result {
    uint32_t magic, version, stage, error, flags, system_control, dram_result, tested_bytes;
    uint32_t failure_address, expected, actual, dram_control, dram_pll_control, framebuffer, width,
        height;
    uint32_t sparse_locations, passes;
};
#define DRAM_RESULT_MAILBOX ((volatile struct dram_diagnostic_result *)0x4800u)
/* Host sets request separately so result clearing cannot overwrite it. */
#define DRAM_REQUEST_FLAGS MMIO32(0x49f0u)
extern unsigned stock_dram_init(unsigned megabytes);
extern void display_init_800x480(uintptr_t framebuffer);
extern void display_color_bars(uint16_t *framebuffer);
extern void display_backlight(int enabled);
extern void display_prepare_from_fel(void);
void display_delay_ms(unsigned duration_ms) {
    /* Match or exceed the BOOT1 busy-loop delay at FEL's CPU clock */
    for (unsigned millisecond = 0; millisecond < duration_ms; millisecond++) {
        for (volatile unsigned i = 0; i < 12000; i++) {
            __asm__ volatile("nop");
        }
    }
}
static int dram_record_mismatch(uintptr_t address, uint32_t expected, uint32_t actual) {
    if (expected == actual) {
        return 0;
    }
    DRAM_RESULT_MAILBOX->error = 3;
    DRAM_RESULT_MAILBOX->failure_address = (uint32_t)address;
    DRAM_RESULT_MAILBOX->expected = expected;
    DRAM_RESULT_MAILBOX->actual = actual;
    return 1;
}
int dram_test(uintptr_t base, unsigned bytes) {
    /* Distinct simultaneous probes catch aliasing of address lines/size first. */
    unsigned offsets[26], probe_count = 0;
    offsets[probe_count++] = 0;
    for (unsigned byte_offset = 4; byte_offset < bytes; byte_offset <<= 1) {
        offsets[probe_count++] = byte_offset;
    }
    offsets[probe_count++] = bytes - 4;
    for (unsigned i = 0; i < probe_count; i++) {
        MMIO32(base + offsets[i]) = 0x6d000000u + i;
    }
    for (unsigned i = 0; i < probe_count; i++) {
        if (dram_record_mismatch(base + offsets[i], 0x6d000000u + i, MMIO32(base + offsets[i]))) {
            return 0;
        }
    }
    DRAM_RESULT_MAILBOX->sparse_locations = probe_count;
    /* Full 32 MB, two address-dependent patterns; caches must be disabled. */
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned byte_offset = 0; byte_offset < bytes; byte_offset += 4) {
            uint32_t value = (uint32_t)(base + byte_offset) ^ 0xa5a55a5au;
            MMIO32(base + byte_offset) = pass ? ~value : value;
        }
        for (unsigned byte_offset = 0; byte_offset < bytes; byte_offset += 4) {
            uint32_t value = (uint32_t)(base + byte_offset) ^ 0xa5a55a5au;
            if (dram_record_mismatch(
                    base + byte_offset, pass ? ~value : value, MMIO32(base + byte_offset))) {
                return 0;
            }
        }
        DRAM_RESULT_MAILBOX->passes = pass + 1;
    }
    DRAM_RESULT_MAILBOX->tested_bytes = bytes;
    return 1;
}
void hello_main(void) {
    unsigned flags = DRAM_REQUEST_FLAGS;
    for (unsigned i = 0; i < sizeof(*DRAM_RESULT_MAILBOX) / 4; i++) {
        ((volatile uint32_t *)DRAM_RESULT_MAILBOX)[i] = 0;
    }
    DRAM_RESULT_MAILBOX->magic = DRAM_RESULT_MAGIC;
    DRAM_RESULT_MAILBOX->version = 1;
    DRAM_RESULT_MAILBOX->stage = 1;
    DRAM_RESULT_MAILBOX->flags = flags;
    unsigned system_control;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(system_control));
    DRAM_RESULT_MAILBOX->system_control = system_control;
    /* Avoid a cached memory-test false pass or changing FEL address translation. */
    if (system_control & 5u) {
        DRAM_RESULT_MAILBOX->error = 1;
        return;
    }
    DRAM_RESULT_MAILBOX->dram_result = stock_dram_init(32);
    DRAM_RESULT_MAILBOX->dram_control = MMIO32(0x01c01000u);
    DRAM_RESULT_MAILBOX->dram_pll_control = MMIO32(0x01c20020u);
    if (DRAM_RESULT_MAILBOX->dram_result != 1) {
        DRAM_RESULT_MAILBOX->error = 2;
        return;
    }
    DRAM_RESULT_MAILBOX->stage = 2;
    if (!(flags & 2u)) {
        return;
    }
    if (!dram_test(DRAM_TEST_BASE, DRAM_TEST_BYTES)) {
        return;
    }
    DRAM_RESULT_MAILBOX->stage = 3;
    if (flags & 4u) {
        display_prepare_from_fel();
        display_backlight(0);
        display_color_bars((uint16_t *)DISPLAY_TEST_FRAMEBUFFER);
        display_init_800x480(DISPLAY_TEST_FRAMEBUFFER);
        display_delay_ms(20);
        display_backlight(1);
        DRAM_RESULT_MAILBOX->framebuffer = DISPLAY_TEST_FRAMEBUFFER;
        DRAM_RESULT_MAILBOX->width = 800;
        DRAM_RESULT_MAILBOX->height = 480;
        DRAM_RESULT_MAILBOX->stage = 4;
    }
}
