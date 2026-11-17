#ifndef USB_PROTOCOL_H
#define USB_PROTOCOL_H
#include <stdint.h>
#define CONTROL_LINE_MAX 128u
/* All command output buffers must provide this capacity */
#define CONTROL_REPLY_MAX 1024u
enum reboot_action { ACTION_NONE, ACTION_FEL, ACTION_NORMAL };
unsigned command_handle(const uint8_t *line, unsigned length, uint8_t *reply, unsigned *action);
unsigned usb_frame_transfer_command(const uint8_t *command, unsigned length, uint8_t *reply);
void usb_frame_transfer_reset(void);
/* Advance transport only; never dispatch a frontend command recursively. */
void usb_service(void);
#define USB_SESSION_LEASE_MS 3000u
#define USB_ACTIVITY_HOLD_MS 250u
enum usb_activity_flag {
    USB_CLIENT_CONNECTED = 1u,
    USB_TRANSMIT_ACTIVE = 2u,
    USB_RECEIVE_ACTIVE = 4u
};
void usb_session_open(void);
void usb_session_close(void);
int usb_is_connected(void);
unsigned usb_activity_flags(void);
#endif
