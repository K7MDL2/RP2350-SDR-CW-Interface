#ifndef USB_AUDIO_CALLBACKS_H
#define USB_AUDIO_CALLBACKS_H

#include <stdint.h>

typedef struct {
    uint32_t packets;
    uint32_t bytes;
    uint32_t accepted_frames;
    uint32_t dropped_frames;
    uint16_t peak;
    uint8_t last_func_id;
} usb_audio_stats_t;

void usb_audio_get_stats(usb_audio_stats_t *stats);
void usb_audio_task(void);

#endif
