#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include <stdbool.h>

#include "wm8960.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_SAVE_ACCEPTED = 0,
    SETTINGS_SAVE_UNCHANGED,
    SETTINGS_SAVE_BUSY,
    SETTINGS_SAVE_UNAVAILABLE
} settings_save_request_t;

typedef enum {
    SETTINGS_SAVE_RESULT_NONE = 0,
    SETTINGS_SAVE_RESULT_OK,
    SETTINGS_SAVE_RESULT_ERROR
} settings_save_result_t;

bool settings_storage_init(wm8960_t *codec);
bool settings_storage_reload(void);
settings_save_request_t settings_storage_request_save(void);
void settings_storage_task(void);
settings_save_result_t settings_storage_take_result(void);
bool settings_storage_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif
