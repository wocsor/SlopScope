#ifndef SCOPE_H
#define SCOPE_H
#include <stdint.h>
void scope_initialize(void);
void scope_poll(void);
void scope_boot_begin(unsigned from_flash);
void scope_boot_cursor_poll(void);
unsigned scope_command(const uint8_t *command, unsigned command_length, uint8_t *reply);
#endif
