#ifndef CW_TEXT_DISPLAY_H
#define CW_TEXT_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Scrolling text log shown across most of the LCD, at an enlarged,
 * easier-to-read scale. Mixes characters from every CW text source: the
 * WinKey emulator's host-buffered text and message-memory replay, the CW
 * decoder (paddle, straight key, and received audio), and (later) a BT
 * keyboard. Which source(s) actually reach the screen is filtered by the
 * cw_display_source setting (see cw_display_source.h).
 *
 * All actual SPI/LCD drawing for this module happens from
 * cw_text_display_task(), which must be called from core1 (see
 * cw_decoder.c's decoder_core loop) so that slow, blocking SPI transfers
 * never share core0 with the time-critical audio pipeline. Every other
 * function here just updates shared state and marks it dirty for the next
 * cw_text_display_task() poll - safe to call from core0 as before.
 *
 * A no-op on boards built without LCD support.
 */
void cw_text_display_init(void);
void cw_text_display_put_char(char c, bool from_winkeyer);

/* Call only from core1; performs any pending drawing work. */
void cw_text_display_task(void);

/* Forces the scrolling log to redraw next poll (e.g. after a popup closes). */
void cw_text_display_redraw(void);

/*
 * While suppressed, incoming characters still update the scrolled-text
 * model but are not drawn, so a popup overlay isn't overwritten.
 */
void cw_text_display_set_suppressed(bool suppressed);

/*
 * Front-panel popup: a short label (and optional bar graph) centered over
 * the scrolling text area. Text display is automatically suppressed while
 * the popup is active, and redrawn once it's hidden.
 */
void cw_text_display_show_popup(const char *text, float fraction, bool show_bar);
void cw_text_display_hide_popup(void);

/* Headphone/speaker output-active icons, stacked below the RX level bar. */
void cw_text_display_set_output_icons(bool headphones_on, bool speakers_on);

#ifdef __cplusplus
}
#endif

#endif

