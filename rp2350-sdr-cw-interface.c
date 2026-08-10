#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "tusb.h"
#include <string.h>

#include "wm8960.h"
#include "board_pins.h"
#include "audio_i2s_test.h"
#include "front_panel_controls.h"
#include "control_console.h"
#include "cw_decoder.h"
#include "settings_storage.h"
#include "usb_audio_callbacks.h"
#include "winkey_emulator.h"

#define MIDI_CW_KEY_NOTE 17u
#define MIDI_PTT_NOTE    18u

static void midi_task(void)
{
#if CFG_TUD_MIDI > 0
    uint8_t packet[4];
    static uint8_t last_echo[4];
    static bool last_echo_valid;
    static uint64_t last_echo_time_us;

    while (tud_midi_packet_read(packet)) {
        uint64_t now_us = time_us_64();
        bool immediate_echo_return =
            last_echo_valid &&
            (now_us - last_echo_time_us) < 15000u &&
            memcmp(packet, last_echo, sizeof(packet)) == 0;

        if (immediate_echo_return) {
            continue;
        }

        uint8_t message = packet[1] & 0xf0u;
        uint8_t note = packet[2] & 0x7fu;
        uint8_t velocity = packet[3] & 0x7fu;

        /*
         * The original CWKeyer uses note 17 on MIDI channel 10. Accept the
         * same note on every channel so it is also easy to test in MIDI-OX.
         */
        if (note == MIDI_CW_KEY_NOTE) {
            if (message == 0x90u) {
                bool pressed = velocity != 0u;
                audio_i2s_set_sidetone_source(
                    AUDIO_SIDETONE_SOURCE_MIDI,
                    pressed
                );
                audio_i2s_set_usb_playback_mute_source(
                    AUDIO_USB_MUTE_SOURCE_MIDI_KEY,
                    pressed
                );
            } else if (message == 0x80u) {
                audio_i2s_set_sidetone_source(
                    AUDIO_SIDETONE_SOURCE_MIDI,
                    false
                );
                audio_i2s_set_usb_playback_mute_source(
                    AUDIO_USB_MUTE_SOURCE_MIDI_KEY,
                    false
                );
            }
        } else if (note == MIDI_PTT_NOTE) {
            if (message == 0x90u) {
                audio_i2s_set_usb_playback_mute_source(
                    AUDIO_USB_MUTE_SOURCE_MIDI_PTT,
                    velocity != 0u
                );
            } else if (message == 0x80u) {
                audio_i2s_set_usb_playback_mute_source(
                    AUDIO_USB_MUTE_SOURCE_MIDI_PTT,
                    false
                );
            }
        }

        (void)tud_midi_packet_write(packet);
        memcpy(last_echo, packet, sizeof(packet));
        last_echo_valid = true;
        last_echo_time_us = now_us;
    }
#endif
}

int main(void)
{
    /*
     * Set sysclock to 153.6 MHz: VCO=768 MHz / postdiv=5.
     *
     * 153600000 / (48000 * 64) = 50 exactly.
     * PIO divider 50 is a pure integer -> no fractional jitter ->
     * no beat-frequency click between RP2350 BCLK and WM8960 MCLK/PLL.
     *
     * USB uses its own fixed 48 MHz PLL, unaffected by this change.
     */
    set_sys_clock_pll(768 * MHZ, 5, 1);

    /*
     * TinyUSB initialiseert het samengestelde Audio + CDC + MIDI-apparaat.
     * pico_stdio_usb staat uit omdat dat eigen USB-descriptors gebruikt.
     */
    stdio_init_all();
    tusb_init();
    sleep_ms(100);

    printf("\r\n");
    printf("============================\r\n");
    printf("WM8960 hardware test\r\n");
    printf(
        "System clock: %lu Hz\r\n",
        (unsigned long)clock_get_hz(clk_sys)
    );

    wm8960_t codec;

    /*
     * WM8960 I2C-control:
     *
     * GPIO2 = SDA
     * GPIO3 = SCL
     * I2C1  = 400 kHz
     */
    printf("Starting I2C1 on GPIO2/GPIO3...\r\n");

    wm8960_bus_init(
        &codec,
        i2c1,
        BOARD_I2C_SDA_PIN,
        BOARD_I2C_SCL_PIN,
        400000
    );

    /*
     * WM8960 configureren.
     * De DAC blijft tijdens initialisatie nog muted.
     */
    printf("Initializing WM8960...\r\n");

    if (!wm8960_init_playback(
            &codec,
            WM8960_RATE_48000,
            16,
            WM8960_OUTPUT_BOTH)) {

        printf("ERROR: WM8960 initialization failed\r\n");

        while (true) {
            printf("WM8960 init error\r\n");
            stdio_flush();
            sleep_ms(1000);
        }
    }

    printf("WM8960 initialized\r\n");

    /*
     * PIO en DMA starten.
     *
     * GPIO18 = BCLK
     * GPIO19 = LRCLK
    * GPIO21 = DAC-data
     */
    printf("Starting PIO I2S...\r\n");

    if (!audio_i2s_start()) {
        printf("ERROR: PIO/DMA I2S start failed\r\n");

        while (true) {
            printf("PIO/DMA error\r\n");
            stdio_flush();
            sleep_ms(1000);
        }
    }

    printf("PIO I2S started\r\n");

    /*
     * Tijdelijke diagnose. Mag later verwijderd worden.
     */
   // audio_i2s_debug();

    /*
     * Hoofdtelefoon- en luidsprekervolume instellen.
     *
     * Begin voor de test met 100%.
     * Later kan dit bijvoorbeeld 70 of 80 worden.
     */
    if (!wm8960_set_headphone_volume(&codec, 80)) {
        printf("ERROR: headphone volume failed\r\n");

        while (true) {
            stdio_flush();
            sleep_ms(1000);
        }
    }

    if (!wm8960_set_speaker_volume(&codec, 80)) {
        printf("ERROR: speaker volume failed\r\n");

        while (true) {
            stdio_flush();
            sleep_ms(1000);
        }
    }

    printf("Headphone and speaker volume set to 80%%\r\n");

    /*
     * De DAC pas unmuten nadat PIO en DMA lopen.
     */
    if (!wm8960_start_playback(&codec)) {
        printf("ERROR: WM8960 unmute failed\r\n");

        while (true) {
            stdio_flush();
            sleep_ms(1000);
        }
    }

    printf("WM8960 playback started\r\n");
    printf("USB speaker audio routed to WM8960\r\n");
#if CFG_TUD_MIDI > 0
    printf("750 Hz sidetone: MIDI note 17 key-down/key-up\r\n");
#endif

    winkey_emulator_init();
    printf("WinKey 2.3 emulator active on CDC\r\n");
    printf("Paddle DIT: GPIO23 to GND, internal pull-up enabled\r\n");
    printf("Paddle DAH: GPIO27 to GND, internal pull-up enabled\r\n");
    printf("Straight key: GPIO22 to GND, internal pull-up enabled\r\n");

    if (settings_storage_init(&codec)) {
        printf("Persistent settings loaded; double-click encoder to save\r\n");
    } else {
        printf("WARNING: persistent settings storage unavailable\r\n");
    }

    if (!front_panel_controls_init(&codec)) {
        printf("ERROR: front-panel controls initialization failed\r\n");
        while (true) {
            tud_task();
            tight_loop_contents();
        }
    }
    printf("Encoder: GPIO15/GPIO6, button GPIO13\r\n");
    printf("Function LEDs: GPIO7/GPIO8/GPIO12/GPIO11/GPIO10\r\n");
    printf("Output LEDs: headphone GPIO24, speaker GPIO25\r\n");

    if (!control_console_init(&codec)) {
        printf("ERROR: control console initialization failed\r\n");
    } else {
        printf("Control console active on CDC interface 1\r\n");
    }

    if (cw_decoder_init()) {
        printf("CW decoder active on core 1 at 750 Hz\r\n");
    }

#if defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
    if (!wm8960_start_capture(&codec)) {
        printf("ERROR: WM8960 capture start failed\r\n");
        while (true) {
            tud_task();
            tight_loop_contents();
        }
    }
    printf("WM8960 microphone capture started on GPIO20\r\n");
    printf("TinyUSB RX + TX Audio ready\r\n");
#else
    printf("TinyUSB audio ready\r\n");
#endif

    while (true) {
        tud_task();
        midi_task();
        winkey_emulator_task();
        control_console_task();
        cw_decoder_task();
        front_panel_controls_task();
        settings_storage_task();
        audio_i2s_task();
        usb_audio_task();
        tight_loop_contents();
    }
}
