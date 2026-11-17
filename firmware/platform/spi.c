#include "scope_internal.h"

enum spi_register {
    SPI_GLOBAL_CONTROL = 0x04,
    SPI_TRANSFER_CONTROL = 0x08,
    SPI_FIFO_CONTROL = 0x18,
    SPI_FIFO_STATUS = 0x1c,
    SPI_CLOCK_CONTROL = 0x24,
    SPI_BURST_COUNT = 0x30,
    SPI_TRANSMIT_COUNT = 0x34,
    SPI_BURST_CONTROL = 0x38,
    SPI_TRANSMIT_FIFO = 0x200,
    SPI_RECEIVE_FIFO = 0x300
};

void spi1_select(void) {
    MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) &= ~0xb0u;
}

void spi1_deselect(void) {
    MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) =
        (MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) & ~0xb0u) | 0x80u;
}

int spi1_initialize(void) {
    for (unsigned pin = 0; pin < 4; pin++) {
        gpio_set_function(GPIOA_BASE, pin, 6);
    }
    MMIO32(0x01c202c0u) |= 1u << 21;
    MMIO32(0x01c20060u) |= 1u << 21;
    MMIO32(SPI1_BASE + SPI_CLOCK_CONTROL) = 0x1010;
    MMIO32(SPI1_BASE + SPI_GLOBAL_CONTROL) |= 0x80000083u;
    uint32_t started_ticks = timer_ticks();
    while (MMIO32(SPI1_BASE + SPI_GLOBAL_CONTROL) & 0x80000000u) {
        if (timer_has_elapsed(started_ticks, 20)) {
            return -1;
        }
    }
    MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) =
        (MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) & ~3u) | 0x45u;
    MMIO32(SPI1_BASE + SPI_FIFO_CONTROL) |= 0x80008000u;
    spi1_deselect();
    scope_state.fpga.spi_ready = 1;
    return 0;
}

int spi1_transfer(const void *transmit_data, void *receive_data, int byte_count) {
    const uint8_t *source = transmit_data;
    uint8_t *destination = receive_data;
    int remaining = byte_count;
    if (!scope_state.fpga.spi_ready || byte_count < 0) {
        return -1;
    }
    while (remaining) {
        unsigned chunk_bytes = remaining > 64 ? 64 : (unsigned)remaining;
        MMIO32(SPI1_BASE + SPI_BURST_COUNT) = chunk_bytes;
        MMIO32(SPI1_BASE + SPI_TRANSMIT_COUNT) = source ? chunk_bytes : 0;
        MMIO32(SPI1_BASE + SPI_BURST_CONTROL) = source ? chunk_bytes : 0;
        if (source) {
            for (unsigned i = 0; i < chunk_bytes; i++) {
                MMIO8(SPI1_BASE + SPI_TRANSMIT_FIFO) = *source++;
            }
        }
        MMIO32(SPI1_BASE + SPI_TRANSFER_CONTROL) |= 0x80000000u;
        uint32_t started_ticks = timer_ticks();
        while ((MMIO32(SPI1_BASE + SPI_FIFO_STATUS) & 255) < chunk_bytes) {
            if (timer_has_elapsed(started_ticks, 20)) {
                scope_state.fpga.error = 4;
                spi1_deselect();
                scope_state.fpga.spi_ready = 0;
                return -1;
            }
        }
        for (unsigned i = 0; i < chunk_bytes; i++) {
            uint8_t received_byte = MMIO8(SPI1_BASE + SPI_RECEIVE_FIFO);
            if (destination) {
                *destination++ = received_byte;
            }
        }
        remaining -= chunk_bytes;
    }
    return byte_count;
}
