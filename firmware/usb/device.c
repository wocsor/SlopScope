#include "board.h"
#include "diagnostics.h"
#include "scope.h"
/* F1C200s MUSB device driver, polled PIO, full/high-speed bulk */
#include "protocol.h"
#include <stddef.h>
#include <stdint.h>
#ifndef USB_HIGH_SPEED
#define USB_HIGH_SPEED 0
#endif
#define USB_CONTROLLER_BASE 0x01c13000u
#define USB_POWER_CONTROL (USB_CONTROLLER_BASE + 0x40)
#define USB_ENDPOINT_INDEX (USB_CONTROLLER_BASE + 0x42)
#define USB_TRANSMIT_INTERRUPT_STATUS (USB_CONTROLLER_BASE + 0x44)
#define USB_RECEIVE_INTERRUPT_STATUS (USB_CONTROLLER_BASE + 0x46)
#define USB_TRANSMIT_INTERRUPT_ENABLE (USB_CONTROLLER_BASE + 0x48)
#define USB_RECEIVE_INTERRUPT_ENABLE (USB_CONTROLLER_BASE + 0x4a)
#define USB_BUS_INTERRUPT_STATUS (USB_CONTROLLER_BASE + 0x4c)
#define USB_BUS_INTERRUPT_ENABLE (USB_CONTROLLER_BASE + 0x50)
#define USB_TRANSMIT_MAX_PACKET (USB_CONTROLLER_BASE + 0x80)
#define USB_TRANSMIT_CONTROL_STATUS (USB_CONTROLLER_BASE + 0x82)
#define USB_RECEIVE_MAX_PACKET (USB_CONTROLLER_BASE + 0x84)
#define USB_RECEIVE_CONTROL_STATUS (USB_CONTROLLER_BASE + 0x86)
#define USB_RECEIVE_BYTE_COUNT (USB_CONTROLLER_BASE + 0x88)
#define USB_DEVICE_ADDRESS (USB_CONTROLLER_BASE + 0x98)
struct usb_statistics {
    uint32_t magic, stage, resets, setups, configured, commands, rx_bytes, tx_bytes, last_request,
        last_csr, reboot_action, errors;
};
#define USB_STATISTICS ((volatile struct usb_statistics *)0x800a0000u)
const uint8_t device_descriptor[] = {
    18, 1, 0, 2, 0, 0, 0, 64, 0xaf, 0xde, 0xbe, 0xba, 0, 1, 1, 2, 3, 1};
const uint8_t configuration_descriptor[] = {9,  2, 32, 0,    1, 1,    0, 0xc0, 0, 9, 4,
                                            0,  0, 2,  0xff, 0, 0,    0, 7,    5, 1, 2,
                                            64, 0, 0,  7,    5, 0x81, 2, 64,   0, 0};
static uint8_t control_buffer[128], command_line[CONTROL_LINE_MAX], reply_buffer[CONTROL_REPLY_MAX],
    receive_packet[512];
static uint8_t benchmark_block[512];
static unsigned benchmark_active;
static const uint8_t *reply_data = reply_buffer;
extern const uint8_t *scope_snapshot_legacy(unsigned *);
extern const uint8_t *scope_factory_table(unsigned *);
extern const uint8_t *scope_snapshot_v2(unsigned *);
extern const uint8_t *scope_snapshot_v3(unsigned *);
extern const uint8_t *scope_snapshot_v4(unsigned *);
extern const uint8_t *scope_snapshot_v5(unsigned *);
extern const uint8_t *scope_snapshot_v6(unsigned *);
extern const uint8_t *scope_snapshot_v7(unsigned *);
extern const uint8_t *scope_record_snapshot(unsigned *);
extern const uint8_t *scope_record_channel_snapshot(unsigned, unsigned *);
static unsigned bulk_max_packet_bytes = 64, force_full_speed;
static unsigned power_before_fallback, high_speed_bus_resets, pre_fallback_resets,
    pre_fallback_setups, pre_fallback_errors, control_status_before_fallback;
static unsigned configured, control_reply_length, control_reply_position, control_transfer_state,
    pending_address;
static unsigned line_length, dropping_oversized_line, reply_length, reply_position,
    reply_needs_zero_length_packet, action, packet_length, packet_position;
static unsigned clear_in_count, clear_out_count, poll_previous, poll_gap_max;
static unsigned poll_window_start, poll_window_max, poll_previous_window_max, poll_slow_count;
static unsigned suspend_count, resume_count, disconnect_count;
static unsigned usb_poll_active;
static unsigned session_requested, session_confirmed;
static unsigned transmit_activity_seen, receive_activity_seen, transmit_packet_pending;
static uint32_t session_reply_ticks, transmit_activity_ticks, receive_activity_ticks;

enum { CONTROL_IDLE, CONTROL_DATA_IN, CONTROL_STATUS_OUT, CONTROL_SET_ADDRESS };
extern void scope_flash_release(void);
extern void enter_brom_fel(void) __attribute__((noreturn));
/* Timer1: free-running 24 MHz down-counter; no interrupts. */
static uint32_t usb_timer_ticks(void) {
    return MMIO32(0x01c20c28u);
}
static void usb_timer_initialize(void) {
    MMIO32(0x01c20c00u) &= ~2u;
    MMIO32(0x01c20c20u) = 0;
    MMIO32(0x01c20c24u) = 0xffffffffu;
    MMIO32(0x01c20c20u) = 7;
}
static __attribute__((noinline)) void usb_delay_ms(unsigned duration_ms) {
    uint32_t start = usb_timer_ticks();
    while (start - usb_timer_ticks() < duration_ms * 24000u) {
        __asm__ volatile("nop");
    }
}
static void usb_fifo_read(unsigned endpoint, uint8_t *destination, unsigned byte_count) {
    for (unsigned i = 0; i < byte_count; i++) {
        destination[i] = MMIO8(USB_CONTROLLER_BASE + 4 * endpoint);
    }
}
static void usb_fifo_write(unsigned endpoint, const uint8_t *source, unsigned byte_count) {
    unsigned offset = 0;
    while (byte_count - offset >= 4) {
        uint32_t word = (unsigned)source[offset] | ((unsigned)source[offset + 1] << 8) |
                        ((unsigned)source[offset + 2] << 16) | ((unsigned)source[offset + 3] << 24);
        MMIO32(USB_CONTROLLER_BASE + 4 * endpoint) = word;
        offset += 4;
    }
    while (offset < byte_count) {
        MMIO8(USB_CONTROLLER_BASE + 4 * endpoint) = source[offset++];
    }
}
static unsigned minimum_unsigned(unsigned left, unsigned right) {
    return left < right ? left : right;
}
unsigned usb_descriptor(unsigned type, unsigned index, uint8_t *destination) {
    const uint8_t *source = 0;
    unsigned descriptor_bytes = 0;
    if (type == 1 && index == 0) {
        source = device_descriptor;
        descriptor_bytes = sizeof device_descriptor;
    } else if ((type == 2 || (USB_HIGH_SPEED && type == 7)) && index == 0) {
        unsigned max_packet_bytes = (MMIO8(USB_POWER_CONTROL) & 0x10) ? 512 : 64;
        if (type == 7) {
            max_packet_bytes = max_packet_bytes == 512 ? 64 : 512;
        }
        for (unsigned i = 0; i < sizeof configuration_descriptor; i++) {
            destination[i] = configuration_descriptor[i];
        }
        destination[1] = type;
        destination[22] = destination[29] = max_packet_bytes & 255;
        destination[23] = destination[30] = max_packet_bytes >> 8;
        return sizeof configuration_descriptor;
    } else if (USB_HIGH_SPEED && type == 6 && index == 0) {
        const uint8_t qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
        for (unsigned i = 0; i < 10; i++) {
            destination[i] = qualifier[i];
        }
        return 10;
    } else if (type == 3) {
        if (index == 0) {
            destination[0] = 4;
            destination[1] = 3;
            destination[2] = 9;
            destination[3] = 4;
            return 4;
        }
        const char *descriptor_text = index == 1   ? "WOCSOR"
                                      : index == 2 ? "DOS1102 bulk control"
                                      : index == 3 ? "DEV0001"
                                                   : 0;
        if (!descriptor_text) {
            return 0;
        }
        descriptor_bytes = 2;
        while (*descriptor_text && descriptor_bytes + 2 <= sizeof control_buffer) {
            destination[descriptor_bytes++] = (uint8_t)*descriptor_text++;
            destination[descriptor_bytes++] = 0;
        }
        destination[0] = (uint8_t)descriptor_bytes;
        destination[1] = 3;
        return descriptor_bytes;
    }
    if (source) {
        for (unsigned i = 0; i < descriptor_bytes; i++) {
            destination[i] = source[i];
        }
    }
    return descriptor_bytes;
}
static void usb_control_stall(void) {
    MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x20;
    control_transfer_state = CONTROL_IDLE;
    USB_STATISTICS->errors++;
}
static void usb_control_send_packet(void) {
    unsigned packet_bytes = minimum_unsigned(control_reply_length - control_reply_position, 64);
    usb_fifo_write(0, control_buffer + control_reply_position, packet_bytes);
    control_reply_position += packet_bytes;
    MMIO16(USB_TRANSMIT_CONTROL_STATUS) =
        (uint16_t)(2u | (control_reply_position == control_reply_length ? 8u : 0u));
    if (control_reply_position == control_reply_length) {
        control_transfer_state = CONTROL_STATUS_OUT;
    }
}
static void usb_control_send_reply(unsigned reply_bytes, unsigned requested_bytes) {
    control_reply_length = minimum_unsigned(reply_bytes, requested_bytes);
    control_reply_position = 0;
    control_transfer_state = CONTROL_DATA_IN;
    usb_control_send_packet();
}
static void usb_bulk_reset_transaction(void) {
#if USB_DIAGNOSTICS
    diagnostics_transaction_aborted();
#endif
    usb_frame_transfer_reset();
    transmit_packet_pending = 0;
    benchmark_active = 0;
    line_length = dropping_oversized_line = reply_length = reply_position =
        reply_needs_zero_length_packet = action = packet_length = packet_position = 0;
}
/* Our command/reply protocol abandons the entire transaction on CLEAR_HALT */
static void usb_bulk_abort_transaction(unsigned endpoint) {
    MMIO8(USB_ENDPOINT_INDEX) = 1;
    MMIO16(USB_TRANSMIT_CONTROL_STATUS) = (uint16_t)(0x2008u | (endpoint == 0x81 ? 0x40u : 0));
    MMIO16(USB_RECEIVE_CONTROL_STATUS) = (uint16_t)(0x10u | (endpoint == 1 ? 0x80u : 0));
    if (endpoint == 0x81) {
        clear_in_count++;
    } else {
        clear_out_count++;
    }
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    usb_bulk_reset_transaction();
}
static void usb_bulk_endpoints_configure(void) {
    MMIO8(USB_ENDPOINT_INDEX) = 1;
    bulk_max_packet_bytes = (USB_HIGH_SPEED && (MMIO8(USB_POWER_CONTROL) & 0x10)) ? 512 : 64;
    MMIO16(USB_TRANSMIT_MAX_PACKET) = bulk_max_packet_bytes;
    MMIO16(USB_RECEIVE_MAX_PACKET) = bulk_max_packet_bytes;
    MMIO8(USB_CONTROLLER_BASE + 0x90) = bulk_max_packet_bytes == 512 ? 6 : 3;
    MMIO16(USB_CONTROLLER_BASE + 0x92) = 8; /* IN FIFO: 64 or 512 bytes, byte offset 64. */
    MMIO8(USB_CONTROLLER_BASE + 0x94) = bulk_max_packet_bytes == 512 ? 6 : 3;
    MMIO16(USB_CONTROLLER_BASE + 0x96) =
        bulk_max_packet_bytes == 512 ? 72
                                     : 16; /* OUT FIFO follows IN; allocation stays within 2 KiB. */
    MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x2048;
    MMIO16(USB_RECEIVE_CONTROL_STATUS) = 0x0090;
    MMIO16(USB_TRANSMIT_INTERRUPT_ENABLE) = 3;
    MMIO16(USB_RECEIVE_INTERRUPT_ENABLE) = 2;
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    usb_bulk_reset_transaction();
}
static unsigned read_u16_le(const uint8_t *bytes) {
    return bytes[0] | ((unsigned)bytes[1] << 8);
}
static void usb_handle_setup_request(const uint8_t *setup_packet) {
    unsigned request_type = setup_packet[0], request = setup_packet[1],
             value = read_u16_le(setup_packet + 2), index = read_u16_le(setup_packet + 4),
             length = read_u16_le(setup_packet + 6);
#if USB_DIAGNOSTICS
    diagnostics_setup_event(request_type, request, value, index, length);
#endif
    USB_STATISTICS->setups++;
#if USB_DIAGNOSTICS
    if (request_type == 0xc0 && request == 0x5a && value <= 3 && (value == 2 || !index)) {
        unsigned reply_bytes = diagnostics_control_reply(control_buffer, value, index);
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
        usb_control_send_reply(reply_bytes, length);
        return;
    }
#endif
    USB_STATISTICS->last_request = (request_type << 8) | request;
    if ((request_type & 0x60) != 0) {
        usb_control_stall();
        return;
    }
    if (request_type == 0x80 && request == 6) {
        unsigned descriptor_bytes = usb_descriptor(value >> 8, value & 255, control_buffer);
        if (!descriptor_bytes) {
            usb_control_stall();
            return;
        }
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
        usb_control_send_reply(descriptor_bytes, length);
        return;
    }
    if (request_type == 0 && request == 5 && !index && !length && value < 128) {
        pending_address = value;
        control_transfer_state = CONTROL_SET_ADDRESS;
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) =
            0x48; /* Apply FADDR after status-IN completion interrupt. */
        return;
    }
    if (request_type == 0 && request == 9 && !index && !length && value <= 1) {
        configured = value;
        usb_session_close();
        USB_STATISTICS->configured = value;
        if (value) {
            usb_bulk_endpoints_configure();
        } else {
            MMIO16(USB_TRANSMIT_INTERRUPT_ENABLE) = 1;
            MMIO16(USB_RECEIVE_INTERRUPT_ENABLE) = 0;
            usb_bulk_reset_transaction();
        }
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x48;
        control_transfer_state = CONTROL_IDLE;
        USB_STATISTICS->stage = value ? 4 : 3;

        return;
    }
    if (request_type == 0x80 && request == 8 && !value && !index) {
        control_buffer[0] = (uint8_t)configured;
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
        usb_control_send_reply(1, length);
        return;
    }
    if (request == 0 && (request_type == 0x80 || request_type == 0x81 || request_type == 0x82) &&
        !value) {
        control_buffer[0] = (request_type == 0x80) ? 1 : 0;
        control_buffer[1] = 0;
        if (request_type == 0x82 && index != 0 && index != 0x80) {
            if (!configured || (index != 1 && index != 0x81)) {
                usb_control_stall();
                return;
            }
            MMIO8(USB_ENDPOINT_INDEX) = 1;
            control_buffer[0] = (index == 0x81 ? MMIO16(USB_TRANSMIT_CONTROL_STATUS) & 0x10
                                               : MMIO16(USB_RECEIVE_CONTROL_STATUS) & 0x20) != 0;
            MMIO8(USB_ENDPOINT_INDEX) = 0;
        }
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
        usb_control_send_reply(2, length);
        return;
    }
    if (request_type == 0x81 && request == 10 && !index && !value && configured) {
        control_buffer[0] = 0;
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
        usb_control_send_reply(1, length);
        return;
    }
    if (request_type == 1 && request == 11 && !index && !value && !length && configured) {
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x48;
        control_transfer_state = CONTROL_IDLE;
        return;
    }
    if (request_type == 2 && request == 1 && !value && !length && (index == 1 || index == 0x81)) {
        usb_bulk_abort_transaction(index);
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x48;
        control_transfer_state = CONTROL_IDLE;
        return;
    }
    usb_control_stall();
}
static void usb_control_endpoint_poll(unsigned interrupt_pending) {
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    unsigned control_status = MMIO16(USB_TRANSMIT_CONTROL_STATUS);
    USB_STATISTICS->last_csr = control_status;
    if (control_status & 4) {
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0;
        control_transfer_state = CONTROL_IDLE;
        return;
    }
    if (control_status & 16) {
        MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x80;
        control_transfer_state = CONTROL_IDLE;
    }
    if (control_transfer_state == CONTROL_SET_ADDRESS && interrupt_pending &&
        !(control_status & 1)) {
        MMIO8(USB_DEVICE_ADDRESS) = (uint8_t)pending_address;
        control_transfer_state = CONTROL_IDLE;
        USB_STATISTICS->stage = 3;
    }
    if (control_status & 1) {
        unsigned received_bytes = MMIO16(USB_RECEIVE_BYTE_COUNT);
        if (received_bytes == 0) {
            MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x40;
            control_transfer_state = CONTROL_IDLE;
            return;
        }
        if (received_bytes != 8) {
            MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x140;
            usb_control_stall();
            return;
        }
        uint8_t setup_packet[8];
        usb_fifo_read(0, setup_packet, 8);
        control_transfer_state = CONTROL_IDLE;
        usb_handle_setup_request(setup_packet);
        return;
    }
    if (control_transfer_state == CONTROL_DATA_IN && !(control_status & 2)) {
        usb_control_send_packet();
    }
}
static void usb_queue_line_too_long_error(void) {
    const char *error_text = "ERR line too long\n";
    reply_length = 0;
    while (*error_text) {
        reply_buffer[reply_length++] = (uint8_t)*error_text++;
    }
    reply_position = reply_needs_zero_length_packet = action = 0;
    reply_data = reply_buffer;
}
static void usb_execute_reboot(unsigned requested_action) __attribute__((noreturn));
static void usb_execute_reboot(unsigned requested_action) {
    USB_STATISTICS->reboot_action = requested_action;
    USB_STATISTICS->stage = 5;

    usb_delay_ms(100);
    MMIO8(USB_POWER_CONTROL) &= (uint8_t)~0x40;
    usb_delay_ms(30);
    scope_flash_release();
    if (requested_action == ACTION_FEL) {
        MMIO32(0x01c20c20u) = 0;
        enter_brom_fel();
    }
    /* F1C200s watchdog: config B4, mode B8, reload B0 (sun6i layout). */
    MMIO32(0x01c20cb4) = (MMIO32(0x01c20cb4) & ~3u) | 1u;
    MMIO32(0x01c20cb8) = (MMIO32(0x01c20cb8) & ~0xf0u) | 1u;
    MMIO32(0x01c20cb0) = 0x14afu;
    for (;;) {
        __asm__ volatile("nop");
    }
}
static void usb_bulk_poll(unsigned dispatch_commands) {
    MMIO8(USB_ENDPOINT_INDEX) = 1;
    /* TXPKTRDY clearing confirms the host consumed the queued packet. */
    if (transmit_packet_pending && !(MMIO16(USB_TRANSMIT_CONTROL_STATUS) & 1)) {
#if USB_DIAGNOSTICS
        diagnostics_packet_consumed();
#endif
        transmit_packet_pending = 0;
        transmit_activity_ticks = usb_timer_ticks();
        transmit_activity_seen = 1;
        if (session_requested) {
            session_reply_ticks = transmit_activity_ticks;
            session_confirmed = 1;
        }
    }
    if (reply_length) {
        if (MMIO16(USB_TRANSMIT_CONTROL_STATUS) & 1) {
            return;
        }
        if (reply_position < reply_length) {
            unsigned packet_bytes =
                minimum_unsigned(reply_length - reply_position, bulk_max_packet_bytes);
            usb_fifo_write(1,
                           reply_data +
                               (benchmark_active ? (reply_position & 511) : reply_position),
                           packet_bytes);
#if USB_DIAGNOSTICS
            diagnostics_packet_queued(
                reply_data + (benchmark_active ? (reply_position & 511) : reply_position),
                packet_bytes,
                reply_position,
                reply_length);
#endif
            reply_position += packet_bytes;
            USB_STATISTICS->tx_bytes += packet_bytes;
            transmit_packet_pending = 1;
            MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x2001;
#if USB_DIAGNOSTICS
            diagnostics_packet_armed(packet_bytes);
#endif
            return;
        }
        if (reply_needs_zero_length_packet) {
            reply_needs_zero_length_packet = 0;
            MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x2001;
#if USB_DIAGNOSTICS
            diagnostics_packet_armed(0);
#endif
            return;
        }
        if (action) {
            if (dispatch_commands) {
                usb_execute_reboot(action);
            }
            return;
        }
        reply_length = reply_position = 0;
    }

    if (!dispatch_commands) {
        return;
    }
    while (packet_position < packet_length) {
        uint8_t command_byte = receive_packet[packet_position++];
        if (command_byte == '\r') {
            continue;
        }
        if (command_byte == '\n') {
            if (dropping_oversized_line) {
                usb_queue_line_too_long_error();
            } else if (line_length) {
#if USB_DIAGNOSTICS
                diagnostics_command_event(command_line, line_length);
#endif
                reply_data = reply_buffer;
                benchmark_active = 0;
                unsigned chunk_reply_bytes =
                    usb_frame_transfer_command(command_line, line_length, reply_buffer);
                if (chunk_reply_bytes) {
                    reply_length = chunk_reply_bytes;
                    action = 0;
                } else if (line_length == 5 && command_line[0] == 'B' && command_line[1] == 'E' &&
                           command_line[2] == 'N' && command_line[3] == 'C' &&
                           command_line[4] == 'H') {
                    for (unsigned i = 0; i < 512; i++) {
                        benchmark_block[i] = i;
                    }
                    benchmark_active = 1;
                    action = 0;
                    reply_data = benchmark_block;
                    reply_length = 1024u * 1024u;
                } else if (line_length == 13 && command_line[0] == 'F' && command_line[1] == 'A' &&
                           command_line[2] == 'C' && command_line[3] == 'T' &&
                           command_line[4] == 'O' && command_line[5] == 'R' &&
                           command_line[6] == 'Y' && command_line[7] == ' ' &&
                           command_line[8] == 'T' && command_line[9] == 'A' &&
                           command_line[10] == 'B' && command_line[11] == 'L' &&
                           command_line[12] == 'E') {
                    action = 0;
                    reply_data = scope_factory_table(&reply_length);
                    if (!reply_length) {
                        reply_data = reply_buffer;
                        const char *error_text = "ERR factory table unavailable\n";
                        while (*error_text) {
                            reply_buffer[reply_length++] = *error_text++;
                        }
                    }
                } else if (line_length == 5 && command_line[0] == 'F' && command_line[1] == 'R' &&
                           command_line[2] == 'A' && command_line[3] == 'M' &&
                           command_line[4] == 'E') {
                    action = 0;
                    reply_data = scope_snapshot_legacy(&reply_length);
                    if (!reply_length) {
                        reply_data = reply_buffer;
                        const char *error_text = "ERR no frame\n";
                        while (*error_text) {
                            reply_buffer[reply_length++] = *error_text++;
                        }
                    }
                } else if (line_length == 6 && command_line[0] == 'F' && command_line[1] == 'R' &&
                           command_line[2] == 'A' && command_line[3] == 'M' &&
                           command_line[4] == 'E' &&
                           (command_line[5] == '2' || command_line[5] == '3' ||
                            command_line[5] == '4' || command_line[5] == '5' ||
                            command_line[5] == '6' || command_line[5] == '7')) {
                    action = 0;
                    reply_data = command_line[5] == '7'   ? scope_snapshot_v7(&reply_length)
                                 : command_line[5] == '6' ? scope_snapshot_v6(&reply_length)
                                 : command_line[5] == '5' ? scope_snapshot_v5(&reply_length)
                                 : command_line[5] == '4' ? scope_snapshot_v4(&reply_length)
                                 : command_line[5] == '3' ? scope_snapshot_v3(&reply_length)
                                                          : scope_snapshot_v2(&reply_length);
                    if (!reply_length) {
                        reply_data = reply_buffer;
                        const char *error_text = "ERR no frame\n";
                        while (*error_text) {
                            reply_buffer[reply_length++] = *error_text++;
                        }
                    }
                } else if (line_length == 16 && command_line[0] == 'R' && command_line[1] == 'E' &&
                           command_line[2] == 'C' && command_line[3] == 'O' &&
                           command_line[4] == 'R' && command_line[5] == 'D' &&
                           command_line[6] == ' ' && command_line[7] == 'F' &&
                           command_line[8] == 'R' && command_line[9] == 'A' &&
                           command_line[10] == 'M' && command_line[11] == 'E' &&
                           command_line[12] == ' ' && command_line[13] == 'C' &&
                           command_line[14] == 'H' &&
                           (command_line[15] == '1' || command_line[15] == '2')) {
                    action = 0;
                    reply_data =
                        scope_record_channel_snapshot(command_line[15] - '1', &reply_length);
                    if (!reply_length) {
                        reply_data = reply_buffer;
                        const char *error_text = "ERR no record\n";
                        while (*error_text) {
                            reply_buffer[reply_length++] = *error_text++;
                        }
                    }
                } else if (line_length == 12 && command_line[0] == 'R' && command_line[1] == 'E' &&
                           command_line[2] == 'C' && command_line[3] == 'O' &&
                           command_line[4] == 'R' && command_line[5] == 'D' &&
                           command_line[6] == ' ' && command_line[7] == 'F' &&
                           command_line[8] == 'R' && command_line[9] == 'A' &&
                           command_line[10] == 'M' && command_line[11] == 'E') {
                    action = 0;
                    reply_data = scope_record_snapshot(&reply_length);
                    if (!reply_length) {
                        reply_data = reply_buffer;
                        const char *error_text = "ERR no record\n";
                        while (*error_text) {
                            reply_buffer[reply_length++] = *error_text++;
                        }
                    }
                } else {
                    reply_length = command_handle(command_line, line_length, reply_buffer, &action);
                }
                reply_position = 0;
                reply_needs_zero_length_packet =
                    (reply_length != 0 && (reply_length & (bulk_max_packet_bytes - 1)) == 0);
                USB_STATISTICS->commands++;
            }
            line_length = dropping_oversized_line = 0;
            if (reply_length) {
                return;
            }
        } else if (!dropping_oversized_line) {
            if (line_length < sizeof command_line) {
                command_line[line_length++] = command_byte;
            } else {
                dropping_oversized_line = 1;
            }
        }
    }
    packet_length = packet_position = 0;
    unsigned receive_status = MMIO16(USB_RECEIVE_CONTROL_STATUS);
    if (receive_status & 0x40) {
        MMIO16(USB_RECEIVE_CONTROL_STATUS) = 0x90;
        return;
    }
    if (receive_status & 1) {
        unsigned packet_bytes = MMIO16(USB_RECEIVE_BYTE_COUNT);
        if (packet_bytes > sizeof receive_packet) {
            MMIO16(USB_RECEIVE_CONTROL_STATUS) = 0x90;
            usb_queue_line_too_long_error();
            USB_STATISTICS->errors++;
            return;
        }
        usb_fifo_read(1, receive_packet, packet_bytes);
#if USB_DIAGNOSTICS
        diagnostics_receive_packet(receive_packet, packet_bytes);
#endif
        packet_length = packet_bytes;
        USB_STATISTICS->rx_bytes += packet_bytes;
        if (packet_bytes) {
            receive_activity_ticks = usb_timer_ticks();
            receive_activity_seen = 1;
        }
        MMIO16(USB_RECEIVE_CONTROL_STATUS) = (uint16_t)(receive_status & ~1u);
    }
}
static void usb_handle_bus_reset(void) {
    usb_session_close();
    configured = control_reply_length = control_reply_position = pending_address = 0;
    control_transfer_state = CONTROL_IDLE;
    usb_bulk_reset_transaction();
    USB_STATISTICS->configured = 0;
    if (MMIO8(USB_POWER_CONTROL) & 0x10) {
        high_speed_bus_resets++;
    }
    USB_STATISTICS->resets++;
    USB_STATISTICS->stage = 2;
    USB_STATISTICS->last_request = (unsigned)MMIO8(USB_POWER_CONTROL) << 16;
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    MMIO8(USB_DEVICE_ADDRESS) = 0;
    MMIO16(USB_TRANSMIT_INTERRUPT_ENABLE) = 1;
    MMIO16(USB_RECEIVE_INTERRUPT_ENABLE) = 0;
    MMIO16(USB_TRANSMIT_INTERRUPT_STATUS) = 0xffff;
    MMIO16(USB_RECEIVE_INTERRUPT_STATUS) = 0xffff;
}
/* Stock tuning values, with suniv PHYCTL at 0x404 (Linux A10 layout) */
static void usb_phy_write(unsigned address, unsigned data, unsigned width) {
    for (unsigned i = 0; i < width; i++) {
        MMIO32(USB_CONTROLLER_BASE + 0x404) =
            (MMIO32(USB_CONTROLLER_BASE + 0x404) & ~0xff00u) | ((address + i) << 8);
        unsigned low = MMIO8(USB_CONTROLLER_BASE + 0x404);
        low = (low & ~0x81u) | ((data & 1) << 7);
        MMIO8(USB_CONTROLLER_BASE + 0x404) = low;
        MMIO8(USB_CONTROLLER_BASE + 0x404) = low | 1;
        MMIO8(USB_CONTROLLER_BASE + 0x404) = low;
        data >>= 1;
    }
}
void usb_init(void) {
    /* BROM already established the analog PHY. Force device mode and FS PIO. */
    MMIO8(USB_POWER_CONTROL) &= (uint8_t)~0x40;
    usb_delay_ms(300);
    MMIO32(0x01c200cc) |= 3;
    MMIO32(0x01c20060) |= 1u << 24;
    MMIO32(0x01c202c0) |= 1u << 24;
    MMIO32(0x01c00004) = (MMIO32(0x01c00004) & ~3u) | 1u;
    MMIO32(0x01c13400) = (MMIO32(0x01c13400) & 0xffffff8fu) | 0x3fc00u;
    if (USB_HIGH_SPEED
#ifdef SCOPE_COLD_BOOT
        || 1
#endif
    ) {
        usb_phy_write(0x0c, 1, 1);
        usb_phy_write(0x20, 0x14, 5);
        usb_phy_write(0x2a, 3, 2);
    }
    MMIO8(USB_CONTROLLER_BASE + 0x43) = 0;
    MMIO16(USB_TRANSMIT_INTERRUPT_ENABLE) = 0;
    MMIO16(USB_RECEIVE_INTERRUPT_ENABLE) = 0;
    MMIO8(USB_BUS_INTERRUPT_ENABLE) = 0;
    MMIO8(USB_POWER_CONTROL) = 0;
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    MMIO8(USB_DEVICE_ADDRESS) = 0;
    MMIO16(USB_TRANSMIT_CONTROL_STATUS) = 0x100;
    MMIO16(USB_TRANSMIT_INTERRUPT_STATUS) = 0xffff;
    MMIO16(USB_RECEIVE_INTERRUPT_STATUS) = 0xffff;
    MMIO8(USB_BUS_INTERRUPT_STATUS) = 0xff;
    usb_handle_bus_reset();
    MMIO8(USB_BUS_INTERRUPT_ENABLE) = 0x27;
    MMIO8(USB_POWER_CONTROL) = USB_HIGH_SPEED && !force_full_speed ? 0x60 : 0x40;
    USB_STATISTICS->stage = 1;
}
static void usb_transport_poll(unsigned dispatch) {
    if (usb_poll_active) {
        return;
    }
    usb_poll_active = 1;
    unsigned current = usb_timer_ticks(), poll_gap_ticks = poll_previous - current;
    if (poll_previous && poll_gap_ticks > poll_gap_max) {
        poll_gap_max = poll_gap_ticks;
    }
    if (poll_previous) {
        if (poll_gap_ticks > poll_window_max) {
            poll_window_max = poll_gap_ticks;
        }
        if (poll_gap_ticks > 240000u) {
            poll_slow_count++;
        }
    }
    if (!poll_window_start || poll_window_start - current >= 24000000u) {
        poll_previous_window_max = poll_window_max;
        poll_window_max = 0;
        poll_window_start = current;
    }
    poll_previous = current;
    unsigned bus_interrupts = MMIO8(USB_BUS_INTERRUPT_STATUS),
             transmit_interrupts = MMIO16(USB_TRANSMIT_INTERRUPT_STATUS),
             receive_interrupts = MMIO16(USB_RECEIVE_INTERRUPT_STATUS);
#if USB_DIAGNOSTICS
    diagnostics_bus_event(bus_interrupts, transmit_interrupts, receive_interrupts);
#endif
    if (bus_interrupts) {
        MMIO8(USB_BUS_INTERRUPT_STATUS) = (uint8_t)bus_interrupts;
    }
    if (transmit_interrupts) {
        MMIO16(USB_TRANSMIT_INTERRUPT_STATUS) = (uint16_t)transmit_interrupts;
    }
    if (receive_interrupts) {
        MMIO16(USB_RECEIVE_INTERRUPT_STATUS) = (uint16_t)receive_interrupts;
    }
    if (bus_interrupts & 1) {
        suspend_count++;
    }
    if (bus_interrupts & 2) {
        resume_count++;
    }
    if (bus_interrupts & 0x20) {
        disconnect_count++;
    }
    if (bus_interrupts & 4) {
        usb_handle_bus_reset();
        usb_poll_active = 0;
        return;
    }
    if (bus_interrupts & 0x20) {
        usb_session_close();
        configured = 0;
        USB_STATISTICS->configured = 0;
        usb_bulk_reset_transaction();
    }
    if (session_confirmed && session_reply_ticks - current >= USB_SESSION_LEASE_MS * 24000u) {
        session_confirmed = 0;
    }
    if (transmit_activity_seen &&
        transmit_activity_ticks - current >= USB_ACTIVITY_HOLD_MS * 24000u) {
        transmit_activity_seen = 0;
    }
    if (receive_activity_seen &&
        receive_activity_ticks - current >= USB_ACTIVITY_HOLD_MS * 24000u) {
        receive_activity_seen = 0;
    }
    usb_control_endpoint_poll(transmit_interrupts & 1);
    if (configured) {
        usb_bulk_poll(dispatch);
    }
    usb_poll_active = 0;
#if USB_DIAGNOSTICS
    diagnostics_uart_poll();
#endif
}
void usb_poll(void) {
    usb_transport_poll(1);
}
void usb_session_open(void) {
    session_requested = 1;
}

void usb_session_close(void) {
    session_requested = session_confirmed = 0;
    transmit_activity_seen = receive_activity_seen = 0;
}

int usb_is_connected(void) {
    return configured && session_requested && session_confirmed &&
           session_reply_ticks - usb_timer_ticks() < USB_SESSION_LEASE_MS * 24000u;
}

unsigned usb_activity_flags(void) {
    if (!usb_is_connected()) {
        return 0;
    }
    unsigned flags = USB_CLIENT_CONNECTED;
    uint32_t current = usb_timer_ticks();
    if (transmit_activity_seen &&
        transmit_activity_ticks - current < USB_ACTIVITY_HOLD_MS * 24000u) {
        flags |= USB_TRANSMIT_ACTIVE;
    }
    if (receive_activity_seen && receive_activity_ticks - current < USB_ACTIVITY_HOLD_MS * 24000u) {
        flags |= USB_RECEIVE_ACTIVE;
    }
    return flags;
}

void usb_service(void) {
    usb_transport_poll(0);
}
unsigned usb_high_speed(void) {
    return USB_HIGH_SPEED && (MMIO8(USB_POWER_CONTROL) & 0x10);
}
void usb_debug(unsigned *values) {
    values[0] = MMIO8(USB_POWER_CONTROL);
    values[1] = force_full_speed;
    values[2] = USB_STATISTICS->resets;
    values[3] = power_before_fallback;
    values[4] = high_speed_bus_resets;
    values[5] = pre_fallback_resets;
    values[6] = pre_fallback_setups;
    values[7] = pre_fallback_errors;
    values[8] = control_status_before_fallback;
}
/* Non-destructive snapshot: interrupt-status reads are deliberately excluded. */
void usb_transport_state(unsigned *values) {
    unsigned index = MMIO8(USB_ENDPOINT_INDEX);
    values[0] = MMIO8(USB_POWER_CONTROL);
    values[1] = configured;
    values[2] = bulk_max_packet_bytes;
    MMIO8(USB_ENDPOINT_INDEX) = 0;
    values[3] = MMIO16(USB_TRANSMIT_CONTROL_STATUS);
    MMIO8(USB_ENDPOINT_INDEX) = 1;
    values[4] = MMIO16(USB_TRANSMIT_CONTROL_STATUS);
    values[5] = MMIO16(USB_RECEIVE_CONTROL_STATUS);
    values[6] = MMIO16(USB_RECEIVE_BYTE_COUNT);
    MMIO8(USB_ENDPOINT_INDEX) = (uint8_t)index;
    values[7] = reply_length - reply_position;
    values[8] = packet_length - packet_position;
    values[9] = line_length;
    values[10] = poll_gap_max / 24;
    values[11] = clear_in_count;
    values[12] = clear_out_count;
    values[13] =
        (poll_window_max > poll_previous_window_max ? poll_window_max : poll_previous_window_max) /
        24;
    values[14] = poll_slow_count;
    values[15] = suspend_count;
    values[16] = resume_count;
    values[17] = disconnect_count;
    values[18] = USB_STATISTICS->resets;
    values[19] = USB_STATISTICS->setups;
    values[20] = USB_STATISTICS->commands;
    values[21] = USB_STATISTICS->errors;
}
extern void scope_poll(void);
extern void scope_boot_begin(unsigned);
void usb_main(void) {
    for (unsigned i = 0; i < sizeof(*USB_STATISTICS) / 4; i++) {
        ((volatile uint32_t *)USB_STATISTICS)[i] = 0;
    }
    USB_STATISTICS->magic = 0x55534231;
    unsigned sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    if (sctlr & 5) {
        USB_STATISTICS->stage = 0xdead;
        USB_STATISTICS->errors = 1;
        for (;;) {
        }
    }
    usb_timer_initialize();
    scope_initialize();
#if USB_DIAGNOSTICS
    diagnostics_initialize();
#endif
    if (MMIO32(0x800a0100u) == 1) {
        usb_delay_ms(10);
        USB_STATISTICS->stage = 0x10;
        MMIO32(0x01c20c20u) = 0;
        return;
    }
    usb_init();
    uint32_t start = usb_timer_ticks();
    unsigned ever_configured = 0;
    unsigned autonomous = MMIO32(0x800a010cu) == 0x4155544fu;
    if (autonomous) {
        scope_boot_begin(MMIO32(0x800a0110u) != 0);
    }
    for (;;) {
        usb_poll();
        /* Give the host its initial enumeration window before boot asset reads */
        if (!autonomous || configured || ever_configured || start - usb_timer_ticks() > 12000000u) {
            scope_poll();
        }
#if USB_DIAGNOSTICS
        diagnostics_poll();
#endif
        if (configured) {
            ever_configured = 1;
        }
        if (USB_HIGH_SPEED && !ever_configured && !force_full_speed &&
            start - usb_timer_ticks() > 6u * 24000000u) {
            pre_fallback_errors = USB_STATISTICS->errors;
            control_status_before_fallback = USB_STATISTICS->last_csr;
            power_before_fallback = MMIO8(USB_POWER_CONTROL);
            pre_fallback_resets = USB_STATISTICS->resets;
            pre_fallback_setups = USB_STATISTICS->setups;
            force_full_speed = 1;
            usb_init();
            start = usb_timer_ticks();
        }
        /* Failed initial enumeration returns to FEL for mailbox inspection/reload. */
        if (!autonomous && !ever_configured && start - usb_timer_ticks() > 15u * 24000000u) {
            usb_execute_reboot(ACTION_FEL);
        }
    }
}
