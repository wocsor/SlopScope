#ifndef SCOPE_SPECTRUM_H
#define SCOPE_SPECTRUM_H
#include <stdint.h>
#define SCOPE_FFT_POINTS 1024u
void scope_fft_transform(int32_t *real, int32_t *imaginary);
uint32_t scope_fft_magnitude(int32_t real, int32_t imaginary);
int scope_fft_db_milli(uint64_t amplitude_uv);
unsigned panel_fft_source(void);
unsigned panel_fft_window(void);
unsigned panel_fft_logarithmic(void);
unsigned panel_fft_db_per_division(void);
int panel_fft_reference_dbv(void);
const char *panel_fft_window_name(void);
void panel_fft_action(unsigned action);
void panel_fft_adjust(int steps);
int panel_fft_prepare(void);
int panel_fft_y(unsigned column, uint64_t linear_scale_uv);
void panel_fft_readouts(void);
unsigned panel_fft_command(const uint8_t *command, unsigned length, uint8_t *reply);
#endif
