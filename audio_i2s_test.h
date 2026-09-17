#ifndef AUDIO_I2S_TEST_H
#define AUDIO_I2S_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool audio_i2s_start(void);
void audio_i2s_stop(void);
void audio_i2s_task(void);
size_t audio_i2s_write_mono16(const int16_t *samples, size_t frame_count);
size_t audio_i2s_read_mono16(int16_t *samples, size_t frame_count);
uint32_t audio_i2s_playback_queued_frames(void);
uint32_t audio_i2s_get_host_underrun_count(void);

#define AUDIO_SIDETONE_SOURCE_LEGACY (1u << 0)
#define AUDIO_SIDETONE_SOURCE_MIDI   (1u << 1)
#define AUDIO_SIDETONE_SOURCE_WINKEY (1u << 2)

#define AUDIO_USB_MUTE_SOURCE_WINKEY  (1u << 0)
#define AUDIO_USB_MUTE_SOURCE_MIDI_KEY (1u << 1)
#define AUDIO_USB_MUTE_SOURCE_MIDI_PTT (1u << 2)
#define AUDIO_USB_MUTE_SOURCE_STORAGE  (1u << 3)

void audio_i2s_set_sidetone(bool enabled);
void audio_i2s_set_sidetone_source(uint32_t source_mask, bool enabled);
void audio_i2s_set_sidetone_frequency(uint32_t frequency_hz);
void audio_i2s_set_sidetone_volume(uint8_t percent);
uint8_t audio_i2s_get_sidetone_volume(void);
void audio_i2s_set_usb_playback_mute_source(
    uint32_t source_mask,
    bool muted
);
void audio_i2s_debug(void);

#ifdef __cplusplus
}
#endif

#endif
