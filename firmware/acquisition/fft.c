#include "fft_tables.h"
#include "spectrum.h"

void usb_service(void);
int64_t scope_ratio(int64_t numerator, uint64_t denominator);
#define FFT_CODE __attribute__((section(".panel_text"), noinline))

FFT_CODE void scope_fft_transform(int32_t *real, int32_t *imaginary) {
    unsigned reversed = 0;
    for (unsigned index = 1; index < SCOPE_FFT_POINTS; index++) {
        unsigned bit = SCOPE_FFT_POINTS >> 1;
        while (reversed & bit) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) {
            int32_t temporary = real[index];
            real[index] = real[reversed];
            real[reversed] = temporary;
            temporary = imaginary[index];
            imaginary[index] = imaginary[reversed];
            imaginary[reversed] = temporary;
        }
        if (!(index & 63)) {
            usb_service();
        }
    }
    unsigned butterflies = 0;
    for (unsigned span = 2; span <= SCOPE_FFT_POINTS; span <<= 1) {
        unsigned half = span / 2;
        unsigned stride = SCOPE_FFT_POINTS / span;
        for (unsigned start = 0; start < SCOPE_FFT_POINTS; start += span) {
            for (unsigned offset = 0; offset < half; offset++) {
                unsigned even = start + offset, odd = even + half;
                unsigned coefficient = offset * stride;
                int32_t product_real = ((int64_t)real[odd] * fft_cosine_q15[coefficient] -
                                        (int64_t)imaginary[odd] * fft_sine_q15[coefficient]) >>
                                       15;
                int32_t product_imaginary =
                    ((int64_t)real[odd] * fft_sine_q15[coefficient] +
                     (int64_t)imaginary[odd] * fft_cosine_q15[coefficient]) >>
                    15;
                real[odd] = (real[even] - product_real) >> 1;
                imaginary[odd] = (imaginary[even] - product_imaginary) >> 1;
                real[even] = (real[even] + product_real) >> 1;
                imaginary[even] = (imaginary[even] + product_imaginary) >> 1;
                if (!(++butterflies & 31)) {
                    usb_service();
                }
            }
        }
    }
}

FFT_CODE uint32_t scope_fft_magnitude(int32_t real, int32_t imaginary) {
    uint64_t remainder = (int64_t)real * real + (int64_t)imaginary * imaginary;
    uint64_t root = 0, bit = (uint64_t)1 << 62;
    while (bit > remainder) {
        bit >>= 2;
    }
    while (bit) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

FFT_CODE int scope_fft_db_milli(uint64_t amplitude_uv) {
    if (!amplitude_uv) {
        return -180000;
    }
    unsigned exponent = 0;
    uint64_t normalized = amplitude_uv;
    while (normalized >= 2) {
        normalized >>= 1;
        exponent++;
    }
    normalized = exponent > 30 ? amplitude_uv >> (exponent - 30) : amplitude_uv << (30 - exponent);
    unsigned fraction = 0;
    for (unsigned bit = 0; bit < 12; bit++) {
        normalized = (normalized * normalized) >> 30;
        if (normalized >= ((uint64_t)2 << 30)) {
            normalized >>= 1;
            fraction |= 1u << (11 - bit);
        }
    }
    int64_t log2_q12 = (int64_t)exponent * 4096 + fraction;
    return (int)scope_ratio(log2_q12 * 6020600 / 4096 - 120000000, 1000);
}
