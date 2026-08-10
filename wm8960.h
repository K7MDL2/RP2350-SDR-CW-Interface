#ifndef WM8960_H
#define WM8960_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Waveshare WM8960 Audio HAT on the Waveshare RP2350-PiZero:
 *
 *   HAT SDA (BCM GPIO2 / physical pin 3) -> RP2350 GPIO2 -> I2C1 SDA
 *   HAT SCL (BCM GPIO3 / physical pin 5) -> RP2350 GPIO3 -> I2C1 SCL
 *
 * The HAT supplies the WM8960 MCLK from its onboard 24 MHz oscillator.
 */
#define WM8960_I2C_ADDRESS_7BIT 0x1Au

typedef enum {
    WM8960_OUTPUT_HEADPHONES = 1,
    WM8960_OUTPUT_SPEAKERS   = 2,
    WM8960_OUTPUT_BOTH       = 3
} wm8960_output_t;

typedef enum {
    WM8960_RATE_44100 = 44100,
    WM8960_RATE_48000 = 48000
} wm8960_sample_rate_t;

typedef struct {
    i2c_inst_t *i2c;
    uint sda_pin;
    uint scl_pin;
    uint32_t i2c_baudrate;
    uint8_t address;

    wm8960_output_t output;
    wm8960_sample_rate_t sample_rate;
    uint8_t bits_per_sample;

    uint8_t headphone_volume_percent;
    uint8_t speaker_volume_percent;
    bool initialized;
    bool muted;
    bool capturing;
} wm8960_t;

/*
 * Initializes the RP2350 I2C peripheral and stores the driver configuration.
 * No codec register is written by this function.
 */
void wm8960_bus_init(
    wm8960_t *codec,
    i2c_inst_t *i2c,
    uint sda_pin,
    uint scl_pin,
    uint32_t baudrate_hz
);

/*
 * Configures playback for 44.1 or 48 kHz, stereo I2S, with the RP2350 as
 * BCLK/LRCLK master and the WM8960 as slave.
 *
 * bits_per_sample may be 16, 20, 24 or 32.
 *
 * The codec remains digitally muted after initialization. Start the RP2350
 * I2S/PIO state machine first, then call wm8960_start_playback().
 */
bool wm8960_init_playback(
    wm8960_t *codec,
    wm8960_sample_rate_t sample_rate,
    uint8_t bits_per_sample,
    wm8960_output_t output
);

/* Unmutes/mutes the WM8960 DAC. */
bool wm8960_start_playback(wm8960_t *codec);
bool wm8960_stop_playback(wm8960_t *codec);
bool wm8960_set_mute(wm8960_t *codec, bool mute);

/* Enables both microphone inputs, mic bias, input mixers and ADCs. */
bool wm8960_start_capture(wm8960_t *codec);

/* Enables headphones, speakers, or both. */
bool wm8960_set_output(wm8960_t *codec, wm8960_output_t output);

/*
 * Volume is 0..100 percent.
 * 0 is effectively muted at the analogue output.
 * 100 is limited to 0 dB; this driver does not use the +1..+6 dB range.
 */
bool wm8960_set_headphone_volume(wm8960_t *codec, uint8_t percent);
bool wm8960_set_speaker_volume(wm8960_t *codec, uint8_t percent);

/*
 * Performs an anti-pop shutdown and powers down the analogue sections.
 * The bus configuration remains active.
 */
bool wm8960_power_down(wm8960_t *codec);

/*
 * Low-level register write. The WM8960 has 7-bit register addresses and
 * 9-bit register values. In normal 2-wire mode its register map is write-only.
 */
bool wm8960_write_register(wm8960_t *codec, uint8_t reg, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
