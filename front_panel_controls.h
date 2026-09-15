#ifndef FRONT_PANEL_CONTROLS_H
#define FRONT_PANEL_CONTROLS_H

#include <stdbool.h>

#include "wm8960.h"

#ifdef __cplusplus
extern "C" {
#endif

bool front_panel_controls_init(wm8960_t *codec);
void front_panel_controls_task(void);
void front_panel_controls_refresh(void);
void LCD_update_all_LEDs(bool on);
void LCD_update_n_LED(uint8_t led_id, bool state);
void LCD_Draw_LED(uint16_t x,uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
