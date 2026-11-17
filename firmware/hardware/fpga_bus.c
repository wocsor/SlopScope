/* Reconstructed from stock V4 app offsets b5394/b53f8/2cc6c */
#include <stdint.h>
extern void spi1_select(void);
extern void spi1_deselect(void);
extern int spi1_transfer(const void *transmit_data, void *receive_data, int byte_count);

int fpga_read_words(unsigned word_address, void *data, int count) {
    uint8_t header[2] = {word_address >> 7, (word_address << 1) | 1};
    int result = -1;
    spi1_select();
    if (spi1_transfer(header, 0, 2) == 2 && spi1_transfer(0, data, count) == count) {
        result = 0;
    }
    spi1_deselect();
    return result;
}

int fpga_write_words(unsigned word_address, const void *data, int count) {
    uint8_t header[2] = {word_address >> 7, word_address << 1};
    int result = -1;
    spi1_select();
    if (spi1_transfer(header, 0, 2) == 2 && spi1_transfer(data, 0, count) == count) {
        result = 0;
    }
    spi1_deselect();
    return result;
}
