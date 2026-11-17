#include <stdint.h>
/* ARM EABI uidivmod returns quotient in r0, remainder in r1 */
uint64_t __aeabi_uidivmod(uint32_t numerator, uint32_t denominator) {
    uint64_t remainder = 0;
    uint32_t quotient = 0;
    if (!denominator) {
        return 0;
    }
    for (unsigned i = 0; i < 32; i++) {
        remainder = (remainder << 1) | (numerator >> 31);
        numerator <<= 1;
        quotient <<= 1;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1;
        }
    }
    return (remainder << 32) | quotient;
}
uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator) {
    return (uint32_t)__aeabi_uidivmod(numerator, denominator);
}
