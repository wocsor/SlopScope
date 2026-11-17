#include "board.h"
/* DOS1102 800x480 display setup reconstructed from BOOT1 */
#include <stddef.h>
#include <stdint.h>
#define DISPLAY_ENGINE_BASE 0x01e60000u
#define LCD_TIMING_CONTROLLER_BASE 0x01c0c000u
#define CLOCK_CONTROL_BASE 0x01c20000u
#define DISPLAY_REGISTER_LOAD (DISPLAY_ENGINE_BASE + 0x870)
#define DISPLAY_REGISTER_LOAD_PENDING 1u
#define DISPLAY_REGISTER_AUTOLOAD_DISABLED 2u
extern void display_delay_ms(unsigned duration_ms);
static void
display_configure_pin(unsigned port_base, unsigned pin, unsigned function, unsigned pull_mode) {
    uintptr_t function_register = port_base + (pin >> 3) * 4,
              pull_register = port_base + 0x1c + (pin >> 4) * 4;
    unsigned function_shift = (pin & 7) * 4, pull_shift = (pin & 15) * 2;
    MMIO32(function_register) &= ~(15u << function_shift);
    MMIO32(function_register) |= function << function_shift;
    MMIO32(pull_register) &= ~(3u << pull_shift);
    MMIO32(pull_register) |= pull_mode << pull_shift;
}
void display_backlight(int enabled) {
    display_configure_pin(0x01c208b4u, 5, 1, 0);
    if (enabled) {
        MMIO32(0x01c208c4u) |= 1u << 5;
    } else {
        MMIO32(0x01c208c4u) &= ~(1u << 5);
    }
}
/* BOOT1 assumes the BOOT0 clock setup has already run */
void display_prepare_from_fel(void) {
    MMIO32(CLOCK_CONTROL_BASE + 0x100) |= 0x05000000u;
    MMIO32(CLOCK_CONTROL_BASE + 0x104) =
        (MMIO32(CLOCK_CONTROL_BASE + 0x104) & ~0x0700000fu) | 0x80000001u;
    MMIO32(CLOCK_CONTROL_BASE + 0x64) |= 0x1000u;
    MMIO32(CLOCK_CONTROL_BASE + 0x2c4) |= 0x1000u;
    display_delay_ms(1);
}
void display_init_800x480(uintptr_t framebuffer) {
    /* BOOT1 0x2138: LCD signals on PD1..PD21, function 2. */
    for (unsigned pin = 1; pin < 22; pin++) {
        display_configure_pin(0x01c2086cu, pin, 2, 0);
    }
    /* BOOT1 0x1f88: clear display backend register window. */
    for (unsigned byte_offset = 0x800; byte_offset < 0x1000; byte_offset += 4) {
        MMIO32(DISPLAY_ENGINE_BASE + byte_offset) = 0;
    }
    MMIO32(DISPLAY_REGISTER_LOAD) = DISPLAY_REGISTER_AUTOLOAD_DISABLED;
    /* 0x1f14: TCON disabled while its registers are prepared. */
    MMIO32(LCD_TIMING_CONTROLLER_BASE) = 0;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 4) = 0;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x44) &= 0x0fffffffu;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x8c) = 0xffffffffu;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0xf4) = 0xffffffffu;
    /* 0x1d34: RGB565 backend, 800x480, one framebuffer layer. */
    MMIO32(CLOCK_CONTROL_BASE + 0x64) |= 0x1000;
    MMIO32(CLOCK_CONTROL_BASE + 0x2c4) |= 0x1000;
    display_delay_ms(1);
    MMIO32(DISPLAY_ENGINE_BASE + 0x800) |= 1;
    MMIO32(DISPLAY_ENGINE_BASE + 0x808) = 799u | (479u << 16);
    MMIO32(DISPLAY_ENGINE_BASE + 0x810) = 799u | (479u << 16);
    MMIO32(DISPLAY_ENGINE_BASE + 0x840) = 800u << 4;
    MMIO32(DISPLAY_ENGINE_BASE + 0x850) = (uint32_t)framebuffer << 3;
    MMIO32(DISPLAY_ENGINE_BASE + 0x860) = (uint32_t)framebuffer >> 29;
    MMIO32(DISPLAY_ENGINE_BASE + 0x8a0) = 0x500;
    MMIO32(DISPLAY_ENGINE_BASE + 0x800) |= 0x100;
    MMIO32(DISPLAY_REGISTER_LOAD) =
        DISPLAY_REGISTER_AUTOLOAD_DISABLED | DISPLAY_REGISTER_LOAD_PENDING;
    MMIO32(DISPLAY_ENGINE_BASE + 0x800) |= 2;
    /* 0x194c: PLL_VIDEO and original panel timings. */
    MMIO32(CLOCK_CONTROL_BASE + 0x10) &= 0x7fffffffu;
    MMIO32(CLOCK_CONTROL_BASE + 0x10) = 0x03004003;
    MMIO32(CLOCK_CONTROL_BASE + 0x10) |= 0x80000000u;
    display_delay_ms(1);
    MMIO32(CLOCK_CONTROL_BASE + 0x64) |= 0x10;
    MMIO32(CLOCK_CONTROL_BASE + 0x118) |= 0x80000000u;
    MMIO32(CLOCK_CONTROL_BASE + 0x2c4) |= 0x10;
    display_delay_ms(1);
    MMIO32(LCD_TIMING_CONTROLLER_BASE) &= ~1u;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x40) = 0x80000000u | (((13u + 23u + 22u) & 31u) << 4);
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x44) = 0xf0000009u;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x48) = 479u | (799u << 16);
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x4c) = 91u | (1101u << 16);
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x50) = 44u | (538u << 17);
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x54) = 21u | (45u << 16);
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x58) = 0;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x60) = 0;
    for (unsigned byte_offset = 0x14; byte_offset <= 0x28; byte_offset += 4) {
        MMIO32(LCD_TIMING_CONTROLLER_BASE + byte_offset) = 0x11111111;
    }
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x2c) = 0x01010000;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x30) = 0x15151111;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x34) = 0x57575555;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x38) = 0x7f7f7777;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x10) = 0x80000000u;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x88) = 0x17000000;
    MMIO32(LCD_TIMING_CONTROLLER_BASE + 0x8c) = 0;
    /* 0x1ee0: global TCON enable. */
    MMIO32(LCD_TIMING_CONTROLLER_BASE) |= 0x80000000u;
}
void display_color_bars(uint16_t *framebuffer) {
    const uint16_t colors[8] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0};
    for (unsigned y = 0; y < 480; y++) {
        for (unsigned x = 0; x < 800; x++) {
            framebuffer[y * 800 + x] = colors[x / 100];
        }
    }
}

/* Present a completed RGB565 buffer through the backend register shadow. */
void display_present(uintptr_t framebuffer) {
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(0) : "memory");
    MMIO32(DISPLAY_ENGINE_BASE + 0x850) = (uint32_t)framebuffer << 3;
    MMIO32(DISPLAY_ENGINE_BASE + 0x860) = (uint32_t)framebuffer >> 29;
    MMIO32(DISPLAY_REGISTER_LOAD) =
        DISPLAY_REGISTER_AUTOLOAD_DISABLED | DISPLAY_REGISTER_LOAD_PENDING;
}

int display_present_pending(void) {
    return (MMIO32(DISPLAY_REGISTER_LOAD) & DISPLAY_REGISTER_LOAD_PENDING) != 0;
}
