#ifndef CW_DISPLAY_SOURCE_H
#define CW_DISPLAY_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which text source(s) are allowed to reach the on-screen CW log, and (for
 * RX_AUDIO/PADDLE_KEY) which audio the decoder itself analyzes.
 */
typedef enum {
    CW_DISPLAY_SOURCE_ALL = 0,
    CW_DISPLAY_SOURCE_RX_AUDIO,
    CW_DISPLAY_SOURCE_PADDLE_KEY,
    CW_DISPLAY_SOURCE_WINKEYER,
    CW_DISPLAY_SOURCE_NONE
} cw_display_source_t;

#define CW_DISPLAY_SOURCE_COUNT 5u

void cw_display_source_set(cw_display_source_t source);
cw_display_source_t cw_display_source_get(void);
const char *cw_display_source_name(cw_display_source_t source);

#ifdef __cplusplus
}
#endif

#endif
