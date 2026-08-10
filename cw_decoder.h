#ifndef CW_DECODER_H
#define CW_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single-channel RX-audio CW decoder. Audio is submitted on core 0, DSP runs
 * on core 1, and cw_decoder_task() publishes completed characters on core 0.
 */
bool cw_decoder_init(void);
void cw_decoder_submit_audio(const int16_t *samples, size_t frame_count);
void cw_decoder_task(void);
void cw_decoder_set_enabled(bool enabled);
bool cw_decoder_get_enabled(void);
bool cw_decoder_set_frequency(uint16_t frequency_hz);
uint16_t cw_decoder_get_frequency(void);
uint8_t cw_decoder_get_wpm(void);
bool cw_decoder_set_fixed_wpm(uint8_t wpm);
uint8_t cw_decoder_get_fixed_wpm(void);
uint32_t cw_decoder_get_dropped_samples(void);

#ifdef __cplusplus
}
#endif

#endif
