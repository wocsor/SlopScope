#include <stdint.h>

/* Small freestanding ABI helpers; volatile accesses prevent self-lowering. */
void __aeabi_memcpy4(void *destination, const void *source, unsigned byte_count) {
    volatile uint8_t *destination_bytes = destination;
    const volatile uint8_t *source_bytes = source;
    while (byte_count--) {
        *destination_bytes++ = *source_bytes++;
    }
}
void __aeabi_memclr4(void *destination, unsigned byte_count) {
    volatile uint8_t *destination_bytes = destination;
    while (byte_count--) {
        *destination_bytes++ = 0;
    }
}
