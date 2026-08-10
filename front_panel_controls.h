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

#ifdef __cplusplus
}
#endif

#endif
