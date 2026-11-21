#ifndef SCOPE_FRONT_PANEL_H
#define SCOPE_FRONT_PANEL_H
#include <stdint.h>
enum front_panel_menu {
    PANEL_MENU_CLOSED,
    PANEL_MENU_CH1,
    PANEL_MENU_CH2,
    PANEL_MENU_TRIGGER,
    PANEL_MENU_HORIZONTAL,
    PANEL_MENU_ACQUIRE,
    PANEL_MENU_DISPLAY,
    PANEL_MENU_UTILITY,
    PANEL_MENU_MEASURE,
    PANEL_MENU_CURSOR,
    PANEL_MENU_MATH,
    PANEL_MENU_AUTOSET,
    PANEL_MENU_COPY,
    PANEL_MENU_TRIGGER_DETAILS
};
enum panel_autoset_state {
    PANEL_AUTOSET_IDLE,
    PANEL_AUTOSET_POSITION,
    PANEL_AUTOSET_RANGE,
    PANEL_AUTOSET_TRIGGER,
    PANEL_AUTOSET_COMPLETE,
    PANEL_AUTOSET_NO_SIGNAL,
    PANEL_AUTOSET_NO_TRIGGER,
    PANEL_AUTOSET_FAILED,
    PANEL_AUTOSET_CANCELLED,
    PANEL_AUTOSET_PREVIEW,
    PANEL_AUTOSET_CLIPPED
};
void front_panel_poll(void);
unsigned front_panel_command(const uint8_t *command, unsigned length, uint8_t *reply);
int front_panel_issue(const char *command);
int front_panel_issue_value(const char *prefix, int32_t value);
unsigned front_panel_command_failed(void);
void front_panel_menu_toggle(unsigned menu);
void front_panel_menu_action(unsigned action);
void front_panel_menu_rotate(int direction);
void front_panel_menu_close(void);
void front_panel_menu_activate(void);
void front_panel_menu_back(void);
void front_panel_menu_state(unsigned *state);
void front_panel_menu_refresh(void);
void front_panel_center_trigger(void);
void front_panel_display_prepare(void);
void front_panel_display_draw(void);
void panel_autoset_start(void);
void panel_autoset_cancel(void);
void panel_autoset_poll(void);
unsigned panel_autoset_state(void);
unsigned panel_autoset_command(const uint8_t *command, unsigned length, uint8_t *reply);
#endif
