#include "control_console.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_i2s_test.h"
#include "cw_decoder.h"
#include "front_panel_controls.h"
#include "settings_storage.h"
#include "tusb.h"
#include "winkey_emulator.h"

#define CONSOLE_CDC_INSTANCE 1u
#define CONSOLE_LINE_BYTES 96u
#define CONSOLE_TX_BYTES 2048u

static wm8960_t *console_codec;
#if CFG_TUD_CDC > 1
static char input_line[CONSOLE_LINE_BYTES];
static size_t input_length;
static uint8_t tx_ring[CONSOLE_TX_BYTES];
static size_t tx_read;
static size_t tx_write;
static size_t tx_count;
static bool was_connected;
static bool save_pending;
static bool cw_line_active;
static bool frequency_link = true;

static void tx_byte(uint8_t value)
{
    if (tx_count == CONSOLE_TX_BYTES) {
        return;
    }
    tx_ring[tx_write] = value;
    tx_write = (tx_write + 1u) % CONSOLE_TX_BYTES;
    ++tx_count;
}

static void tx_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        tx_byte((uint8_t)*text++);
    }
}

static void tx_printf(const char *format, ...)
{
    char buffer[192];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0) {
        return;
    }
    tx_text(buffer);
}

static const char *output_name(wm8960_output_t output)
{
    switch (output) {
        case WM8960_OUTPUT_HEADPHONES: return "headphones";
        case WM8960_OUTPUT_SPEAKERS: return "speakers";
        case WM8960_OUTPUT_BOTH: return "both";
        default: return "unknown";
    }
}

static void show_status(void)
{
    tx_text("CW KEYER STATUS\r\n");
    tx_printf("speed: %u WPM\r\n", winkey_emulator_get_speed());
    tx_printf(
        "master-volume: %u%%\r\n",
        console_codec->headphone_volume_percent
    );
    tx_printf(
        "sidetone-volume: %u%%\r\n",
        audio_i2s_get_sidetone_volume()
    );
    tx_printf(
        "tone-frequency: %u Hz\r\n",
        winkey_emulator_get_sidetone_frequency()
    );
    tx_printf("output: %s\r\n", output_name(console_codec->output));
    tx_printf(
        "mode: %s\r\n",
        winkey_emulator_mode_name(winkey_emulator_get_mode())
    );
    tx_printf(
        "paddle-swap: %s\r\n",
        winkey_emulator_get_paddle_swap() ? "on" : "off"
    );
    tx_printf("weight: %u%%\r\n", winkey_emulator_get_weight());
    tx_printf(
        "decoder: %s\r\n",
        cw_decoder_get_enabled() ? "on" : "off"
    );
    tx_printf("decoder-frequency: %u Hz\r\n", cw_decoder_get_frequency());
    tx_printf("frequency-link: %s\r\n", frequency_link ? "on" : "off");
    tx_printf("decoder-speed: %u WPM\r\n", cw_decoder_get_wpm());
    tx_printf(
        "decoder-speed-mode: %s\r\n",
        cw_decoder_get_fixed_wpm() == 0u ? "auto" : "fixed"
    );
    tx_printf(
        "decoder-dropped-samples: %lu\r\n",
        (unsigned long)cw_decoder_get_dropped_samples()
    );
}

static void show_help(void)
{
    tx_text(
        "Commands:\r\n"
        "  show\r\n"
        "  help\r\n"
        "  set speed 5..99\r\n"
        "  set master-volume 0..100\r\n"
        "  set sidetone-volume 0..100\r\n"
        "  set tone-frequency 300..1200\r\n"
        "  set cw-frequency 300..1200\r\n"
        "  set frequency-link on|off\r\n"
        "  set output headphones|speakers|both\r\n"
        "  set mode iambic-a|iambic-b|ultimatic|bug\r\n"
        "  set paddle-swap on|off\r\n"
        "  set weight 0..100\r\n"
        "  set decoder on|off\r\n"
        "  set decoder-frequency 300..1200\r\n"
        "  set decoder-speed auto|5..60\r\n"
        "  save\r\n"
        "  reload\r\n"
    );
}

static bool parse_number(
    const char *text,
    unsigned minimum,
    unsigned maximum,
    unsigned *value)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (*end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = (unsigned)parsed;
    return true;
}

static void command_set(const char *setting, const char *argument)
{
    unsigned value;

    if (setting == NULL || argument == NULL) {
        tx_text("ERROR usage: set <setting> <value>\r\n");
        return;
    }

    if (strcmp(setting, "speed") == 0) {
        if (!parse_number(argument, 5u, 99u, &value)) {
            tx_text("ERROR speed must be 5..99\r\n");
            return;
        }
        winkey_emulator_set_speed((uint8_t)value);
        tx_printf("OK speed=%u\r\n", value);
    } else if (strcmp(setting, "master-volume") == 0) {
        if (!parse_number(argument, 0u, 100u, &value)) {
            tx_text("ERROR master-volume must be 0..100\r\n");
            return;
        }
        bool ok = wm8960_set_headphone_volume(console_codec, (uint8_t)value);
        ok = wm8960_set_speaker_volume(console_codec, (uint8_t)value) && ok;
        if (ok) {
            tx_printf("OK master-volume=%u\r\n", value);
        } else {
            tx_text("ERROR codec write failed\r\n");
        }
    } else if (strcmp(setting, "sidetone-volume") == 0) {
        if (!parse_number(argument, 0u, 100u, &value)) {
            tx_text("ERROR sidetone-volume must be 0..100\r\n");
            return;
        }
        audio_i2s_set_sidetone_volume((uint8_t)value);
        tx_printf("OK sidetone-volume=%u\r\n", value);
    } else if (strcmp(setting, "tone-frequency") == 0) {
        if (!parse_number(argument, 300u, 1200u, &value)) {
            tx_text("ERROR tone-frequency must be 300..1200\r\n");
            return;
        }
        winkey_emulator_set_sidetone_frequency((uint16_t)value);
        if (frequency_link) {
            (void)cw_decoder_set_frequency((uint16_t)value);
            tx_printf("OK tone-frequency=%u decoder-frequency=%u\r\n", value, value);
        } else {
            tx_printf("OK tone-frequency=%u\r\n", value);
        }
    } else if (strcmp(setting, "cw-frequency") == 0) {
        if (!parse_number(argument, 300u, 1200u, &value)) {
            tx_text("ERROR cw-frequency must be 300..1200\r\n");
            return;
        }
        winkey_emulator_set_sidetone_frequency((uint16_t)value);
        (void)cw_decoder_set_frequency((uint16_t)value);
        tx_printf(
            "OK cw-frequency=%u (sidetone and decoder)\r\n",
            value
        );
    } else if (strcmp(setting, "weight") == 0) {
        if (!parse_number(argument, 0u, 100u, &value)) {
            tx_text("ERROR weight must be 0..100\r\n");
            return;
        }
        winkey_emulator_set_weight((uint8_t)value);
        tx_printf("OK weight=%u\r\n", value);
    } else if (strcmp(setting, "decoder-frequency") == 0) {
        if (!parse_number(argument, 300u, 1200u, &value)) {
            tx_text("ERROR decoder-frequency must be 300..1200\r\n");
            return;
        }
        (void)cw_decoder_set_frequency((uint16_t)value);
        if (frequency_link) {
            winkey_emulator_set_sidetone_frequency((uint16_t)value);
            tx_printf("OK decoder-frequency=%u tone-frequency=%u\r\n", value, value);
        } else {
            tx_printf("OK decoder-frequency=%u\r\n", value);
        }
    } else if (strcmp(setting, "frequency-link") == 0) {
        if (strcmp(argument, "on") == 0) {
            frequency_link = true;
            uint16_t frequency = winkey_emulator_get_sidetone_frequency();
            (void)cw_decoder_set_frequency(frequency);
            tx_printf("OK frequency-link=on frequency=%u\r\n", frequency);
        } else if (strcmp(argument, "off") == 0) {
            frequency_link = false;
            tx_text("OK frequency-link=off\r\n");
        } else {
            tx_text("ERROR frequency-link must be on or off\r\n");
        }
    } else if (strcmp(setting, "decoder") == 0) {
        if (strcmp(argument, "on") == 0) {
            cw_decoder_set_enabled(true);
        } else if (strcmp(argument, "off") == 0) {
            cw_decoder_set_enabled(false);
        } else {
            tx_text("ERROR decoder must be on or off\r\n");
            return;
        }
        tx_printf("OK decoder=%s\r\n", argument);
    } else if (strcmp(setting, "decoder-speed") == 0) {
        if (strcmp(argument, "auto") == 0) {
            (void)cw_decoder_set_fixed_wpm(0u);
            tx_text("OK decoder-speed=auto\r\n");
        } else if (parse_number(argument, 5u, 60u, &value)) {
            (void)cw_decoder_set_fixed_wpm((uint8_t)value);
            tx_printf("OK decoder-speed=%u fixed\r\n", value);
        } else {
            tx_text("ERROR decoder-speed must be auto or 5..60\r\n");
        }
    } else if (strcmp(setting, "output") == 0) {
        wm8960_output_t output;
        if (strcmp(argument, "headphones") == 0) {
            output = WM8960_OUTPUT_HEADPHONES;
        } else if (strcmp(argument, "speakers") == 0) {
            output = WM8960_OUTPUT_SPEAKERS;
        } else if (strcmp(argument, "both") == 0) {
            output = WM8960_OUTPUT_BOTH;
        } else {
            tx_text("ERROR output must be headphones, speakers or both\r\n");
            return;
        }
        if (wm8960_set_output(console_codec, output)) {
            front_panel_controls_refresh();
            tx_printf("OK output=%s\r\n", output_name(output));
        } else {
            tx_text("ERROR codec write failed\r\n");
        }
    } else if (strcmp(setting, "mode") == 0) {
        winkey_mode_t mode;
        if (strcmp(argument, "iambic-a") == 0) {
            mode = WINKEY_MODE_IAMBIC_A;
        } else if (strcmp(argument, "iambic-b") == 0) {
            mode = WINKEY_MODE_IAMBIC_B;
        } else if (strcmp(argument, "ultimatic") == 0) {
            mode = WINKEY_MODE_ULTIMATIC;
        } else if (strcmp(argument, "bug") == 0) {
            mode = WINKEY_MODE_BUG;
        } else {
            tx_text("ERROR mode must be iambic-a, iambic-b, ultimatic or bug\r\n");
            return;
        }
        winkey_emulator_set_mode(mode);
        tx_printf("OK mode=%s\r\n", winkey_emulator_mode_name(mode));
    } else if (strcmp(setting, "paddle-swap") == 0) {
        if (strcmp(argument, "on") == 0) {
            winkey_emulator_set_paddle_swap(true);
        } else if (strcmp(argument, "off") == 0) {
            winkey_emulator_set_paddle_swap(false);
        } else {
            tx_text("ERROR paddle-swap must be on or off\r\n");
            return;
        }
        tx_printf("OK paddle-swap=%s\r\n", argument);
    } else {
        tx_printf("ERROR unknown setting: %s\r\n", setting);
    }
}

static void execute_command(char *line)
{
    char *command = strtok(line, " ");
    char *setting = strtok(NULL, " ");
    char *argument = strtok(NULL, " ");
    char *extra = strtok(NULL, " ");

    if (command == NULL) {
        return;
    }
    if (extra != NULL) {
        tx_text("ERROR too many arguments\r\n");
    } else if (strcmp(command, "show") == 0 && setting == NULL) {
        show_status();
    } else if (strcmp(command, "help") == 0 && setting == NULL) {
        show_help();
    } else if (strcmp(command, "set") == 0) {
        command_set(setting, argument);
    } else if (strcmp(command, "save") == 0 && setting == NULL) {
        settings_save_request_t result = settings_storage_request_save();
        if (result == SETTINGS_SAVE_ACCEPTED) {
            save_pending = true;
            tx_text("OK save started\r\n");
        } else if (result == SETTINGS_SAVE_UNCHANGED) {
            tx_text("OK settings unchanged\r\n");
        } else if (result == SETTINGS_SAVE_BUSY) {
            tx_text("ERROR save busy\r\n");
        } else {
            tx_text("ERROR storage unavailable\r\n");
        }
    } else if (strcmp(command, "reload") == 0 && setting == NULL) {
        if (settings_storage_reload()) {
            if (frequency_link) {
                (void)cw_decoder_set_frequency(
                    winkey_emulator_get_sidetone_frequency()
                );
            }
            front_panel_controls_refresh();
            tx_text("OK saved settings reloaded\r\n");
        } else {
            tx_text("ERROR reload failed\r\n");
        }
    } else {
        tx_text("ERROR unknown command; type help\r\n");
    }
}

static void receive_task(void)
{
    if (tud_cdc_n_available(CONSOLE_CDC_INSTANCE) != 0u && cw_line_active) {
        tx_text("\r\ncw> ");
        cw_line_active = false;
    }
    while (tud_cdc_n_available(CONSOLE_CDC_INSTANCE) != 0u) {
        int32_t received = tud_cdc_n_read_char(CONSOLE_CDC_INSTANCE);
        if (received < 0) {
            break;
        }
        char character = (char)received;
        if (character == '\r' || character == '\n') {
            if (input_length != 0u) {
                input_line[input_length] = '\0';
                tx_text("\r\n");
                execute_command(input_line);
                input_length = 0u;
                tx_text("cw> ");
            }
        } else if (character == '\b' || character == 0x7f) {
            if (input_length != 0u) {
                --input_length;
                tx_text("\b \b");
            }
        } else if ((uint8_t)character >= 0x20u &&
                   (uint8_t)character < 0x7fu) {
            if (input_length + 1u < sizeof(input_line)) {
                if (character >= 'A' && character <= 'Z') {
                    character = (char)(character - 'A' + 'a');
                }
                input_line[input_length++] = character;
                tx_byte((uint8_t)character);
            }
        }
    }
}

static void transmit_task(void)
{
    bool wrote = false;
    while (tx_count != 0u &&
           tud_cdc_n_write_available(CONSOLE_CDC_INSTANCE) != 0u) {
        size_t available = tud_cdc_n_write_available(CONSOLE_CDC_INSTANCE);
        size_t contiguous = CONSOLE_TX_BYTES - tx_read;
        size_t count = tx_count < contiguous ? tx_count : contiguous;
        if (count > available) {
            count = available;
        }
        uint32_t accepted = tud_cdc_n_write(
            CONSOLE_CDC_INSTANCE,
            &tx_ring[tx_read],
            (uint32_t)count
        );
        if (accepted == 0u) {
            break;
        }
        tx_read = (tx_read + accepted) % CONSOLE_TX_BYTES;
        tx_count -= accepted;
        wrote = true;
    }
    if (wrote) {
        tud_cdc_n_write_flush(CONSOLE_CDC_INSTANCE);
    }
}
#endif

bool control_console_init(wm8960_t *codec)
{
#if CFG_TUD_CDC > 1
    console_codec = codec;
    if (codec != NULL && frequency_link) {
        (void)cw_decoder_set_frequency(
            winkey_emulator_get_sidetone_frequency()
        );
    }
    return codec != NULL;
#else
    (void)codec;
    console_codec = NULL;
    return false;
#endif
}

void control_console_task(void)
{
#if CFG_TUD_CDC > 1
    if (frequency_link && console_codec != NULL) {
        uint16_t sidetone_frequency =
            winkey_emulator_get_sidetone_frequency();
        if (cw_decoder_get_frequency() != sidetone_frequency) {
            (void)cw_decoder_set_frequency(sidetone_frequency);
        }
    }

    bool connected = tud_cdc_n_connected(CONSOLE_CDC_INSTANCE);
    if (connected && !was_connected) {
        input_length = 0u;
        cw_line_active = false;
        tx_read = 0u;
        tx_write = 0u;
        tx_count = 0u;
        tx_text("\r\nRP2350 SDR CW Interface Console\r\nType help for commands.\r\ncw> ");
    }
    was_connected = connected;
    if (!connected || console_codec == NULL) {
        return;
    }

    receive_task();
    if (save_pending) {
        settings_save_result_t result = settings_storage_take_result();
        if (result == SETTINGS_SAVE_RESULT_OK) {
            save_pending = false;
            tx_text("\r\nEVENT settings-saved\r\ncw> ");
        } else if (result == SETTINGS_SAVE_RESULT_ERROR) {
            save_pending = false;
            tx_text("\r\nERROR save failed\r\ncw> ");
        }
    }
    transmit_task();
#endif
}

void control_console_publish_event(const char *event_text)
{
#if CFG_TUD_CDC > 1
    if (event_text == NULL || *event_text == '\0') {
        return;
    }
    if (!tud_cdc_n_connected(CONSOLE_CDC_INSTANCE)) {
        return;
    }
    tx_text("\r\nEVENT ");
    tx_text(event_text);
    tx_text("\r\ncw> ");
#else
    (void)event_text;
#endif
}

void control_console_publish_cw_char(char character)
{
#if CFG_TUD_CDC > 1
    if (!tud_cdc_n_connected(CONSOLE_CDC_INSTANCE)) {
        return;
    }
    if (!cw_line_active) {
        tx_text("\r\nCW: ");
        cw_line_active = true;
    }
    tx_byte((uint8_t)character);
#else
    (void)character;
#endif
}

void control_console_end_cw_line(void)
{
#if CFG_TUD_CDC > 1
    if (!cw_line_active) {
        return;
    }
    tx_text("\r\ncw> ");
    cw_line_active = false;
#endif
}
