#include "diagnostics.h"
#if USB_DIAGNOSTICS
#include "scope_internal.h"

#define UART0_BASE 0x01c25000u
#define UART_LINE_STATUS (UART0_BASE + 0x14)
#define UART_TRANSMIT_EMPTY 0x20u
#define DIAGNOSTIC_QUEUE_BYTES 2048u

static uint8_t transmit_queue[DIAGNOSTIC_QUEUE_BYTES];
static unsigned queue_read, queue_write, dropped_snapshots;
static unsigned uart_ready, main_loops, last_bus_flags, last_setup_request, last_command_prefix;
static uint32_t last_snapshot_ticks;
static unsigned uart_clock_hz, uart_divisor, transmitted_bytes;
static unsigned sof_observations, frame_observations, last_usb_frame;
static uint32_t last_sof_ticks, last_frame_change_ticks;
static uint32_t packet_trace[24] = {0x31544b50u, 1};
#define EVENT_WORDS 16u
#define EVENT_COUNT 64u
enum diagnostic_event_kind {
    EVENT_RECEIVE = 1,
    EVENT_COMMAND,
    EVENT_QUEUED,
    EVENT_ARMED,
    EVENT_CONSUMED,
    EVENT_BUS,
    EVENT_SETUP,
    EVENT_ABORT,
    EVENT_TRIGGER
};
static uint32_t event_history[EVENT_COUNT][EVENT_WORDS];
static unsigned event_write_index, event_count, event_sequence, trace_frozen;
static unsigned packet_pending, packet_armed_ticks, dump_phase, dump_event_index;
static uint32_t fault_summary[16], frozen_packet[24];
static uint8_t last_command[128], frozen_command[128];
static unsigned last_command_length, frozen_command_length;

static void
diagnostic_event(unsigned kind, unsigned argument0, unsigned argument1, unsigned argument2) {
    if (trace_frozen || scope_state.boot.stage != BOOT_ACQUIRING) {
        return;
    }
    uint32_t *entry = event_history[event_write_index];
    entry[0] = 0x31545645u;
    entry[1] = ++event_sequence;
    entry[2] = ~timer_ticks();
    entry[3] = kind;
    entry[4] = argument0;
    entry[5] = argument1;
    entry[6] = argument2;
    unsigned previous_index = MMIO8(0x01c13042u);
    MMIO8(0x01c13042u) = 0;
    entry[7] = MMIO16(0x01c13082u);
    MMIO8(0x01c13042u) = 1;
    entry[8] = MMIO16(0x01c13082u);
    entry[9] = MMIO16(0x01c13086u);
    entry[10] = MMIO16(0x01c13088u);
    MMIO8(0x01c13042u) = previous_index;
    entry[11] = MMIO8(0x01c13040u);
    entry[12] = MMIO8(0x01c13041u);
    entry[13] = scope_state.frame.sequence;
    entry[14] = last_command_prefix;
    entry[15] = packet_trace[2];
    event_write_index = (event_write_index + 1) % EVENT_COUNT;
    if (event_count < EVENT_COUNT) {
        event_count++;
    }
}

static unsigned diagnostic_transmit_status(void) {
    unsigned previous_index = MMIO8(0x01c13042u);
    MMIO8(0x01c13042u) = 1;
    unsigned status = MMIO16(0x01c13082u);
    MMIO8(0x01c13042u) = previous_index;
    return status;
}

static void diagnostic_freeze(unsigned reason) {
    if (trace_frozen || scope_state.boot.stage != BOOT_ACQUIRING) {
        return;
    }
    unsigned pending_ms = packet_pending ? (packet_armed_ticks - timer_ticks()) / 24000u : 0;
    diagnostic_event(EVENT_TRIGGER, reason, pending_ms, diagnostic_transmit_status());
    trace_frozen = 1;
    const uint32_t summary[16] = {0x31544c46u,
                                  1,
                                  reason,
                                  timer_milliseconds(),
                                  event_count,
                                  event_sequence,
                                  packet_trace[2],
                                  packet_trace[3],
                                  pending_ms,
                                  scope_state.frame.sequence,
                                  last_bus_flags,
                                  last_setup_request,
                                  last_command_prefix,
                                  MMIO32(0x01c20cb4u),
                                  MMIO32(0x01c20cb8u),
                                  last_command_length};
    for (unsigned index = 0; index < 16; index++) {
        fault_summary[index] = summary[index];
    }
    for (unsigned index = 0; index < 24; index++) {
        frozen_packet[index] = packet_trace[index];
    }
    frozen_command_length = last_command_length;
    for (unsigned index = 0; index < frozen_command_length; index++) {
        frozen_command[index] = last_command[index];
    }
}

void diagnostics_receive_packet(const uint8_t *data, unsigned byte_count) {
    diagnostic_event(EVENT_RECEIVE, byte_count, scope_crc32(data, byte_count), 0);
}

void diagnostics_packet_armed(unsigned byte_count) {
    packet_pending = byte_count != 0;
    packet_armed_ticks = timer_ticks();
    diagnostic_event(EVENT_ARMED, byte_count, packet_trace[2], 0);
}

void diagnostics_transaction_aborted(void) {
    diagnostic_event(EVENT_ABORT, packet_pending, 0, 0);
    packet_pending = 0;
}

void diagnostics_packet_queued(const uint8_t *data,
                               unsigned byte_count,
                               unsigned offset,
                               unsigned reply_length) {
    packet_trace[2]++;
    packet_trace[4] = timer_milliseconds();
    packet_trace[6] = offset;
    packet_trace[7] = reply_length;
    packet_trace[8] = byte_count;
    packet_trace[9] = scope_crc32(data, byte_count);
    for (unsigned index = 0; index < 4; index++) {
        packet_trace[10 + index] = 0;
        for (unsigned byte = 0; byte < 4 && index * 4 + byte < byte_count; byte++) {
            packet_trace[10 + index] |= (unsigned)data[index * 4 + byte] << (byte * 8);
        }
    }
    packet_trace[14] = MMIO16(0x01c13080u);
    packet_trace[15] = MMIO16(0x01c13084u);
    packet_trace[16] = MMIO8(0x01c13090u);
    packet_trace[17] = MMIO8(0x01c13094u);
    packet_trace[18] = MMIO16(0x01c13092u);
    packet_trace[19] = MMIO16(0x01c13096u);
    packet_trace[20] = MMIO16(0x01c13082u);
    packet_trace[21] = MMIO16(0x01c13086u);
    packet_trace[22] = MMIO8(0x01c13042u);
    packet_trace[23] = MMIO32(0x01c00004u);
    diagnostic_event(EVENT_QUEUED, byte_count, packet_trace[9], offset);
}

void diagnostics_packet_consumed(void) {
    packet_trace[3]++;
    packet_trace[5] = timer_milliseconds();
    packet_pending = 0;
    diagnostic_event(EVENT_CONSUMED, packet_trace[2], packet_trace[3], 0);
}

static unsigned diagnostic_uart_clock_hz(void) {
    unsigned cpu_pll = MMIO32(0x01c20000u), peripheral_pll = MMIO32(0x01c20028u);
    unsigned cpu_source = (MMIO32(0x01c20050u) >> 16) & 3u;
    unsigned cpu_hz = cpu_source == 0 ? 32768u : 24000000u;
    if (cpu_source >= 2) {
        cpu_hz = 24000000u * (((cpu_pll >> 8) & 31u) + 1) * (((cpu_pll >> 4) & 3u) + 1);
        cpu_hz /= ((cpu_pll & 3u) + 1) << ((cpu_pll >> 16) & 3u);
    }
    unsigned bus = MMIO32(0x01c20054u), source = (bus >> 12) & 3u;
    unsigned parent_hz = source == 0 ? 32768u : source == 1 ? 24000000u : cpu_hz;
    if (source == 3) {
        parent_hz =
            24000000u * (((peripheral_pll >> 8) & 31u) + 1) * (((peripheral_pll >> 4) & 3u) + 1);
        parent_hz /= ((bus >> 6) & 3u) + 1;
    }
    unsigned apb_divisor = (bus >> 8) & 3u;
    return (parent_hz >> ((bus >> 4) & 3u)) >> (apb_divisor ? apb_divisor : 1);
}

void diagnostics_initialize(void) {
    unsigned clock_hz = uart_clock_hz = diagnostic_uart_clock_hz();
    unsigned divisor = uart_divisor = (clock_hz + 115200u * 8u) / (115200u * 16u);
    if (!divisor || divisor > 65535) {
        return;
    }
    MMIO32(0x01c20068u) |= 1u << 20;
    MMIO32(0x01c202d0u) |= 1u << 20;
    gpio_set_function(GPIOF_BASE, 4, 3);
    MMIO32(UART0_BASE + 4) = 0;
    MMIO32(UART0_BASE + 0x0c) = 0x83;
    MMIO32(UART0_BASE) = divisor & 255u;
    MMIO32(UART0_BASE + 4) = divisor >> 8;
    MMIO32(UART0_BASE + 0x0c) = 3;
    MMIO32(UART0_BASE + 8) = 7;
    uart_ready = 1;
}

void diagnostics_uart_poll(void) {
    if (!uart_ready || !(MMIO32(UART_LINE_STATUS) & UART_TRANSMIT_EMPTY)) {
        return;
    }
    for (unsigned budget = 0; budget < 16 && queue_read != queue_write; budget++) {
        MMIO32(UART0_BASE) = transmit_queue[queue_read];
        transmitted_bytes++;
        queue_read = (queue_read + 1) % DIAGNOSTIC_QUEUE_BYTES;
    }
}

void diagnostics_bus_event(unsigned flags,
                           unsigned transmit_interrupts,
                           unsigned receive_interrupts) {
    uint32_t now_ticks = timer_ticks();
    unsigned frame = MMIO16(0x01c13054u) & 0x7ffu;
    if (!frame_observations || frame != last_usb_frame) {
        last_usb_frame = frame;
        last_frame_change_ticks = now_ticks;
        frame_observations++;
    }
    if (flags & 8u) {
        sof_observations++;
        last_sof_ticks = now_ticks;
    }
    if (flags & 0x27u) {
        last_bus_flags = flags;
        diagnostic_event(EVENT_BUS, flags, transmit_interrupts, receive_interrupts);
        if (packet_pending && (flags & 0x25u)) {
            diagnostic_freeze(2);
        }
    }
}

void diagnostics_setup_event(
    unsigned request_type, unsigned request, unsigned value, unsigned index, unsigned length) {
    if (request_type != 0xc0 || request != 0x5a) {
        last_setup_request = (request_type << 24) | (request << 16) | value;
        diagnostic_event(EVENT_SETUP, last_setup_request, (index << 16) | length, 0);
    }
}

void diagnostics_command_event(const uint8_t *command, unsigned length) {
    last_command_prefix = 0;
    for (unsigned index = 0; index < length && index < 4; index++) {
        last_command_prefix |= (unsigned)command[index] << (index * 8);
    }
    last_command_length = length < sizeof last_command ? length : sizeof last_command;
    for (unsigned index = 0; index < last_command_length; index++) {
        last_command[index] = command[index];
    }
    diagnostic_event(EVENT_COMMAND, length, scope_crc32(command, length), last_command_prefix);
}

static void diagnostic_snapshot(uint32_t *words) {
    unsigned transport[22];
    usb_transport_state(transport);
    const unsigned transport_indices[19] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 10, 18, 19, 20, 21, 15, 16, 17};
    words[0] = 0x44425355u;
    words[1] = 1;
    words[2] = timer_milliseconds();
    words[3] = main_loops;
    words[4] = scope_state.frame.sequence;
    words[5] = scope_state.capture.state;
    words[6] = scope_state.boot.stage;
    for (unsigned index = 0; index < 19; index++) {
        words[index + 7] = transport[transport_indices[index]];
    }
    words[26] = MMIO8(0x01c13041u);
    words[27] = MMIO32(0x01c13400u);
    words[28] = last_bus_flags;
    words[29] = last_setup_request;
    words[30] = last_command_prefix;
}

unsigned diagnostics_control_reply(uint8_t *reply, unsigned page, unsigned offset) {
    if (page == 2) {
        unsigned length;
        const uint8_t *frame = scope_diagnostic_saved_frame(&length);
        if (!frame || offset >= length) {
            return 0;
        }
        length -= offset;
        if (length > 124) {
            length = 124;
        }
        for (unsigned index = 0; index < length; index++) {
            reply[index] = frame[offset + index];
        }
        return length;
    }
    uint32_t words[USB_DIAGNOSTIC_WORDS];
    unsigned count = USB_DIAGNOSTIC_WORDS;
    if (page == 3) {
        count = 24;
        for (unsigned index = 0; index < count; index++) {
            words[index] = packet_trace[index];
        }
    } else if (page == 1) {
        count = 24;
        const unsigned uart_status[24] = {
            0x54524155u,
            1,
            uart_ready,
            uart_clock_hz,
            uart_divisor,
            transmitted_bytes,
            queue_read,
            queue_write,
            dropped_snapshots,
            uart_ready ? MMIO32(UART_LINE_STATUS) : 0,
            uart_ready ? MMIO32(UART0_BASE + 0x0c) : 0,
            uart_ready ? MMIO32(UART0_BASE + 0x7c) : 0,
            uart_ready ? MMIO32(UART0_BASE + 0x10) : 0,
            MMIO32(0x01c20068u),
            MMIO32(0x01c202d0u),
            MMIO32(GPIOF_BASE),
            MMIO32(GPIOF_BASE + 0x10),
            MMIO32(GPIOF_BASE + 0x1c),
            MMIO32(0x01c20000u),
            MMIO32(0x01c20028u),
            MMIO32(0x01c20050u),
            MMIO32(0x01c20054u),
            uart_ready ? MMIO32(UART0_BASE + 0xa4) : 0,
            uart_ready ? MMIO32(UART0_BASE + 0x80) : 0,
        };
        for (unsigned index = 0; index < count; index++) {
            words[index] = uart_status[index];
        }
    } else {
        diagnostic_snapshot(words);
    }
    for (unsigned index = 0; index < count; index++) {
        for (unsigned byte = 0; byte < 4; byte++) {
            reply[index * 4 + byte] = words[index] >> (byte * 8);
        }
    }
    return count * 4;
}

static void diagnostic_queue_line(const char *prefix, const uint32_t *words, unsigned word_count) {
    char line[USB_DIAGNOSTIC_WORDS * 9 + 48];
    unsigned length = 0;
    while (*prefix) {
        line[length++] = *prefix++;
    }
    for (unsigned index = 0; index < word_count; index++) {
        length += reply_write_hex32((uint8_t *)line + length, words[index]);
        line[length++] = ' ';
    }
    length += reply_write_hex32((uint8_t *)line + length, dropped_snapshots);
    line[length++] = '\n';
    unsigned free_bytes =
        (queue_read + DIAGNOSTIC_QUEUE_BYTES - queue_write - 1) % DIAGNOSTIC_QUEUE_BYTES;
    if (length > free_bytes) {
        dropped_snapshots++;
        return;
    }
    for (unsigned index = 0; index < length; index++) {
        transmit_queue[queue_write] = (uint8_t)line[index];
        queue_write = (queue_write + 1) % DIAGNOSTIC_QUEUE_BYTES;
    }
}

static void diagnostic_dump_poll(void) {
    unsigned free_bytes =
        (queue_read + DIAGNOSTIC_QUEUE_BYTES - queue_write - 1) % DIAGNOSTIC_QUEUE_BYTES;
    if (!trace_frozen || dump_phase == 4 || free_bytes < 1024) {
        return;
    }
    if (dump_phase == 0) {
        diagnostic_queue_line("DBG FAULT ", fault_summary, 16);
        dump_phase++;
    } else if (dump_phase == 1) {
        const char *prefix = "DBG CMD ";
        while (*prefix) {
            transmit_queue[queue_write] = (uint8_t)*prefix++;
            queue_write = (queue_write + 1) % DIAGNOSTIC_QUEUE_BYTES;
        }
        for (unsigned index = 0; index < frozen_command_length; index++) {
            unsigned byte = frozen_command[index];
            transmit_queue[queue_write] = byte >= 32 && byte <= 126 ? byte : '.';
            queue_write = (queue_write + 1) % DIAGNOSTIC_QUEUE_BYTES;
        }
        transmit_queue[queue_write] = '\n';
        queue_write = (queue_write + 1) % DIAGNOSTIC_QUEUE_BYTES;
        dump_phase++;
    } else if (dump_phase == 2) {
        diagnostic_queue_line("DBG FROZEN ", frozen_packet, 24);
        dump_phase++;
    } else {
        unsigned index =
            (event_write_index + EVENT_COUNT - event_count + dump_event_index) % EVENT_COUNT;
        diagnostic_queue_line("DBG EVT ", event_history[index], EVENT_WORDS);
        if (++dump_event_index == event_count) {
            dump_phase++;
        }
    }
}

void diagnostics_poll(void) {
    main_loops++;
    diagnostics_uart_poll();
    if (!trace_frozen && packet_pending && packet_armed_ticks - timer_ticks() >= 500u * 24000u &&
        (diagnostic_transmit_status() & 1u)) {
        diagnostic_freeze(1);
    }
    diagnostic_dump_poll();
    if (!uart_ready || !timer_has_elapsed(last_snapshot_ticks, 1000)) {
        return;
    }
    last_snapshot_ticks = timer_ticks();
    uint32_t words[USB_DIAGNOSTIC_WORDS];
    diagnostic_snapshot(words);
    diagnostic_queue_line("DBG USB ", words, USB_DIAGNOSTIC_WORDS);
    diagnostic_queue_line("DBG TX ", packet_trace, 24);
    const uint32_t health[12] = {0x31544c48u,
                                 1,
                                 timer_milliseconds(),
                                 MMIO32(0x01c20000u),
                                 MMIO32(0x01c20028u),
                                 MMIO32(0x01c20050u),
                                 MMIO32(0x01c20054u),
                                 MMIO32(0x01c20cb4u),
                                 MMIO32(0x01c20cb8u),
                                 uart_clock_hz,
                                 uart_divisor,
                                 transmitted_bytes};
    diagnostic_queue_line("DBG HEALTH ", health, 12);
    uint32_t now_ticks = timer_ticks();
    const uint32_t link[10] = {
        0x314b4e4cu,
        1,
        timer_milliseconds(),
        MMIO16(0x01c13054u) & 0x7ffu,
        sof_observations,
        sof_observations ? (last_sof_ticks - now_ticks) / 24000u : 0xffffffffu,
        frame_observations,
        frame_observations ? (last_frame_change_ticks - now_ticks) / 24000u : 0xffffffffu,
        MMIO8(0x01c13040u),
        MMIO8(0x01c13041u)};
    diagnostic_queue_line("DBG LINK ", link, 10);
}
#endif
