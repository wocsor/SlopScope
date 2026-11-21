#ifndef SCOPE_SCREENSHOT_H
#define SCOPE_SCREENSHOT_H
#include <stdint.h>
void screenshot_begin(void);
void screenshot_poll(void);
unsigned screenshot_busy(void);
unsigned screenshot_ready(void);
unsigned screenshot_command(const uint8_t *command, unsigned length, uint8_t *reply);
#endif
