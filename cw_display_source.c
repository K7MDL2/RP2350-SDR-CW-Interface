#include "cw_display_source.h"

#include "cw_decoder.h"

static cw_display_source_t current_source = CW_DISPLAY_SOURCE_ALL;

/*
 * The decoder's core1 Goertzel loop has no use for WINKEYER/NONE selections
 * (no audio is fed to it in those modes either, see audio_i2s_test.c), so
 * shut it down rather than let it keep spinning on nothing.
 */
static void sync_decoder_enabled(cw_display_source_t source)
{
    bool needs_decoder =
        source == CW_DISPLAY_SOURCE_ALL ||
        source == CW_DISPLAY_SOURCE_RX_AUDIO ||
        source == CW_DISPLAY_SOURCE_PADDLE_KEY;
    cw_decoder_set_enabled(needs_decoder);
}

void cw_display_source_set(cw_display_source_t source)
{
    if ((unsigned)source >= CW_DISPLAY_SOURCE_COUNT) {
        return;
    }
    current_source = source;
    sync_decoder_enabled(source);
}

cw_display_source_t cw_display_source_get(void)
{
    return current_source;
}

const char *cw_display_source_name(cw_display_source_t source)
{
    switch (source) {
        case CW_DISPLAY_SOURCE_ALL: return "ALL";
        case CW_DISPLAY_SOURCE_RX_AUDIO: return "RX";
        case CW_DISPLAY_SOURCE_PADDLE_KEY: return "KEY";
        case CW_DISPLAY_SOURCE_WINKEYER: return "WK";
        case CW_DISPLAY_SOURCE_NONE: return "OFF";
        default: return "?";
    }
}
