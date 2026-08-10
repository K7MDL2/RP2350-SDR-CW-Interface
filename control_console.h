#ifndef CONTROL_CONSOLE_H
#define CONTROL_CONSOLE_H

#include <stdbool.h>

#include "wm8960.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CDC instance 1. CDC instance 0 remains reserved for WinKey. */
bool control_console_init(wm8960_t *codec);
void control_console_task(void);

/*
 * Future decoder modules can publish complete, newline-free event text here
 * from core 0. A core-1 decoder should cross to core 0 through its own SPSC
 * event queue first, keeping TinyUSB calls on core 0.
 */
void control_console_publish_event(const char *event_text);
void control_console_publish_cw_char(char character);
void control_console_end_cw_line(void);

#ifdef __cplusplus
}
#endif

#endif
