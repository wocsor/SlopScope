#ifndef SCOPE_DIAGNOSTICS_H
#define SCOPE_DIAGNOSTICS_H
#include <stdint.h>
#ifndef USB_DIAGNOSTICS
#define USB_DIAGNOSTICS 0
#endif
#define USB_DIAGNOSTIC_WORDS 31u
#if USB_DIAGNOSTICS
void diagnostics_initialize(void);
void diagnostics_poll(void);
void diagnostics_uart_poll(void);
void diagnostics_bus_event(unsigned flags,
                           unsigned transmit_interrupts,
                           unsigned receive_interrupts);
void diagnostics_setup_event(
    unsigned request_type, unsigned request, unsigned value, unsigned index, unsigned length);
void diagnostics_command_event(const uint8_t *command, unsigned length);
void diagnostics_receive_packet(const uint8_t *data, unsigned byte_count);
void diagnostics_packet_armed(unsigned byte_count);
void diagnostics_transaction_aborted(void);
void diagnostics_packet_queued(const uint8_t *data,
                               unsigned byte_count,
                               unsigned offset,
                               unsigned reply_length);
void diagnostics_packet_consumed(void);
unsigned diagnostics_control_reply(uint8_t *reply, unsigned page, unsigned offset);
const uint8_t *scope_diagnostic_saved_frame(unsigned *length);
#else
static inline void diagnostics_initialize(void) {
}
static inline void diagnostics_poll(void) {
}
static inline void diagnostics_uart_poll(void) {
}
static inline void
diagnostics_bus_event(unsigned flags, unsigned transmit_interrupts, unsigned receive_interrupts) {
    (void)flags;
    (void)transmit_interrupts;
    (void)receive_interrupts;
}
static inline void diagnostics_setup_event(
    unsigned request_type, unsigned request, unsigned value, unsigned index, unsigned length) {
    (void)request_type;
    (void)request;
    (void)value;
    (void)index;
    (void)length;
}
static inline void diagnostics_command_event(const uint8_t *command, unsigned length) {
    (void)command;
    (void)length;
}
#endif
#endif
