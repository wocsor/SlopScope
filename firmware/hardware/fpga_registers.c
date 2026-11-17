#include "scope_internal.h"

int fpga_write_register(unsigned address, unsigned value) {
    uint8_t low_halfword[2] = {value >> 8, value}, high_halfword[2] = {value >> 24, value >> 16};
    if (fpga_write_words(address >> 1, low_halfword, 2)) {
        return -1;
    }
    return fpga_write_words((address >> 1) + 1, high_halfword, 2);
}

int fpga_read_register(unsigned address, uint32_t *value) {
    uint8_t low_halfword[2], high_halfword[2];
    if (fpga_read_words(address >> 1, low_halfword, 2) ||
        fpga_read_words((address >> 1) + 1, high_halfword, 2)) {
        return -1;
    }
    *value = ((uint32_t)high_halfword[0] << 24) | ((uint32_t)high_halfword[1] << 16) |
             ((uint32_t)low_halfword[0] << 8) | low_halfword[1];
    return 0;
}
