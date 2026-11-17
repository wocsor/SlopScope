#include "platform.h"

void gpio_set_function(unsigned port_base, unsigned pin, unsigned function) {
    unsigned function_shift = (pin & 7) * 4;
    MMIO32(port_base + 4 * (pin >> 3)) =
        (MMIO32(port_base + 4 * (pin >> 3)) & ~(15u << function_shift)) |
        (function << function_shift);
    unsigned pull_shift = (pin & 15) * 2;
    MMIO32(port_base + 0x1c + 4 * (pin >> 4)) &= ~(3u << pull_shift);
}

void gpio_set_pull(unsigned port_base, unsigned pin, unsigned pull_mode) {
    unsigned pull_shift = (pin & 15) * 2;
    MMIO32(port_base + 0x1c + 4 * (pin >> 4)) =
        (MMIO32(port_base + 0x1c + 4 * (pin >> 4)) & ~(3u << pull_shift)) |
        (pull_mode << pull_shift);
}

void gpio_write(unsigned port_base, unsigned pin, unsigned is_high) {
    if (is_high) {
        MMIO32(port_base + 0x10) |= 1u << pin;
    } else {
        MMIO32(port_base + 0x10) &= ~(1u << pin);
    }
}

unsigned gpio_read(unsigned port_base, unsigned pin) {
    return (MMIO32(port_base + 0x10) >> pin) & 1;
}

void gpio_settle(void) {
    for (volatile unsigned i = 0; i < 12; i++) {
        __asm__ volatile("nop");
    }
}
