#include "front_panel_controls.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_i2s_test.h"
#include "board_pins.h"
#include "cw_display_source.h"
#include "cw_text_display.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "settings_storage.h"
#include "winkey_emulator.h"
#include "lcd_extra.h"
#include "lcd.h"

#define ENCODER_A_PIN BOARD_ENCODER_A_PIN
#define ENCODER_B_PIN BOARD_ENCODER_B_PIN
#define ENCODER_BUTTON_PIN BOARD_ENCODER_BUTTON_PIN

#define BUTTON_DEBOUNCE_MS 20u
#define DOUBLE_CLICK_MS 400u
#define POPUP_TIMEOUT_MS 2000u

typedef enum
{
    CONTROL_MASTER = 0,
    CONTROL_SIDETONE,
    CONTROL_FREQUENCY,
    CONTROL_SPEED,
    CONTROL_OUTPUT,
    CONTROL_DECODER_SOURCE,
    CONTROL_NONE
} control_selection_t;

/* Output icons (headphone/speaker), stacked below the RX level bar. */
#define OUTPUT_ICON_X          146u
#define OUTPUT_ICON_WIDTH      12u
#define OUTPUT_ICON_HEIGHT     9u
#define OUTPUT_ICON_HP_Y       59u
#define OUTPUT_ICON_SPK_Y      69u
#define OUTPUT_ICON_ON_COLOR   BLUE
#define OUTPUT_ICON_OFF_COLOR  LGRAY

/*
 * Popup overlay covering the scrolling text area while a function is being
 * viewed/adjusted; cw_text_display_redraw() restores the text once it hides.
 */
#define POPUP_X              2u
#define POPUP_Y              2u
#define POPUP_WIDTH          144u
#define POPUP_HEIGHT         66u
#define POPUP_LABEL_Y        (POPUP_Y + 4u)
#define POPUP_BAR_Y          (POPUP_Y + 34u)
#define POPUP_BAR_X          (POPUP_X + 4u)
#define POPUP_BAR_WIDTH      (POPUP_WIDTH - 8u)
#define POPUP_BAR_HEIGHT     24u
#define POPUP_TEXT_COLOR     WHITE
#define POPUP_BAR_FILL_COLOR GREEN
#define POPUP_BAR_BACK_COLOR DARKGRAY

/* Full-step quadrature decoder; four valid transitions produce one detent. */
static const int8_t encoder_transition[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0};

static wm8960_t *front_panel_codec;
static control_selection_t selection = CONTROL_NONE;
static uint8_t encoder_history;
static int8_t encoder_accumulator;
static bool button_candidate;
static bool button_pressed;
static uint32_t button_candidate_since;
static bool click_pending;
static uint32_t first_click_ms;
static bool awaiting_save_result;
static bool popup_visible;
static uint32_t popup_deadline_ms;

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static void draw_output_icon(uint16_t y, bool on)
{
    uint16_t color = on ? OUTPUT_ICON_ON_COLOR : OUTPUT_ICON_OFF_COLOR;
    LCD_Fill(
        OUTPUT_ICON_X,
        y,
        OUTPUT_ICON_X + OUTPUT_ICON_WIDTH - 1u,
        y + OUTPUT_ICON_HEIGHT - 1u,
        color
    );
}

static void update_output_icons(void)
{
    draw_output_icon(
        OUTPUT_ICON_HP_Y,
        (front_panel_codec->output & WM8960_OUTPUT_HEADPHONES) != 0);
    draw_output_icon(
        OUTPUT_ICON_SPK_Y,
        (front_panel_codec->output & WM8960_OUTPUT_SPEAKERS) != 0);
}

static void hide_popup(void)
{
    if (!popup_visible)
    {
        return;
    }
    popup_visible = false;
    LCD_Fill(
        POPUP_X,
        POPUP_Y,
        POPUP_X + POPUP_WIDTH - 1u,
        POPUP_Y + POPUP_HEIGHT - 1u,
        BLACK
    );
    cw_text_display_set_suppressed(false);
    cw_text_display_redraw();
}

static void arm_popup_timeout(void)
{
    popup_visible = true;
    popup_deadline_ms = to_ms_since_boot(get_absolute_time()) + POPUP_TIMEOUT_MS;
}

static void show_message_popup(const char *text)
{
    cw_text_display_set_suppressed(true);
    LCD_Fill(
        POPUP_X,
        POPUP_Y,
        POPUP_X + POPUP_WIDTH - 1u,
        POPUP_Y + POPUP_HEIGHT - 1u,
        BLACK
    );
    cw_text_display_draw_large_text(
        POPUP_X + 4u, POPUP_LABEL_Y + 8u, text, POPUP_TEXT_COLOR);
    arm_popup_timeout();
}

static void selection_label_and_fraction(
    char *text, size_t text_size, float *fraction)
{
    switch (selection)
    {
    case CONTROL_MASTER:
    {
        uint8_t percent = front_panel_codec->headphone_volume_percent;
        snprintf(text, text_size, "VOL:%u%%", percent);
        *fraction = (float)percent / 100.0f;
        break;
    }
    case CONTROL_SIDETONE:
    {
        uint8_t percent = audio_i2s_get_sidetone_volume();
        snprintf(text, text_size, "TONE:%u%%", percent);
        *fraction = (float)percent / 100.0f;
        break;
    }
    case CONTROL_FREQUENCY:
    {
        uint16_t hz = winkey_emulator_get_sidetone_frequency();
        snprintf(text, text_size, "HZ:%u", hz);
        *fraction = (float)(hz - 300u) / (float)(1200u - 300u);
        break;
    }
    case CONTROL_SPEED:
    {
        uint8_t wpm = winkey_emulator_get_speed();
        snprintf(text, text_size, "WPM:%u", wpm);
        *fraction = (float)(wpm - 5u) / (float)(99u - 5u);
        break;
    }
    case CONTROL_OUTPUT:
    {
        wm8960_output_t output = front_panel_codec->output;
        const char *name =
            output == WM8960_OUTPUT_HEADPHONES ? "HP" :
            output == WM8960_OUTPUT_SPEAKERS ? "SPK" : "BOTH";
        snprintf(text, text_size, "OUT:%s", name);
        *fraction =
            (float)((int)output - (int)WM8960_OUTPUT_HEADPHONES) /
            (float)((int)WM8960_OUTPUT_BOTH - (int)WM8960_OUTPUT_HEADPHONES);
        break;
    }
    case CONTROL_DECODER_SOURCE:
    {
        cw_display_source_t source = cw_display_source_get();
        snprintf(text, text_size, "SRC:%s", cw_display_source_name(source));
        *fraction = (float)source / (float)(CW_DISPLAY_SOURCE_COUNT - 1u);
        break;
    }
    case CONTROL_NONE:
    default:
        text[0] = '\0';
        *fraction = 0.0f;
        break;
    }
}

static void show_function_popup(void)
{
    if (selection == CONTROL_NONE)
    {
        hide_popup();
        return;
    }

    char text[16];
    float fraction = 0.0f;
    selection_label_and_fraction(text, sizeof(text), &fraction);

    cw_text_display_set_suppressed(true);
    LCD_Fill(
        POPUP_X,
        POPUP_Y,
        POPUP_X + POPUP_WIDTH - 1u,
        POPUP_Y + POPUP_HEIGHT - 1u,
        BLACK
    );
    cw_text_display_draw_large_text(
        POPUP_X + 4u, POPUP_LABEL_Y, text, POPUP_TEXT_COLOR);
    cw_text_display_draw_bar(
        POPUP_BAR_X, POPUP_BAR_Y, POPUP_BAR_WIDTH, POPUP_BAR_HEIGHT,
        fraction, POPUP_BAR_FILL_COLOR, POPUP_BAR_BACK_COLOR
    );
    arm_popup_timeout();
}

static void save_on_double_click(uint32_t now)
{
    (void)now;
    settings_save_request_t request = settings_storage_request_save();
    switch (request)
    {
    case SETTINGS_SAVE_ACCEPTED:
        awaiting_save_result = true;
        show_message_popup("SAVING");
        break;
    case SETTINGS_SAVE_UNCHANGED:
        show_message_popup("NO CHANGE");
        break;
    case SETTINGS_SAVE_BUSY:
    case SETTINGS_SAVE_UNAVAILABLE:
    default:
        show_message_popup("SAVE ERR");
        break;
    }
}

static void handle_save_result(void)
{
    settings_save_result_t result = settings_storage_take_result();
    if (result == SETTINGS_SAVE_RESULT_NONE)
    {
        return;
    }
    awaiting_save_result = false;
    show_message_popup(result == SETTINGS_SAVE_RESULT_OK ? "SAVED" : "SAVE ERR");
}

static void select_next_function(void)
{
    if (selection == CONTROL_NONE)
    {
        selection = CONTROL_MASTER;
    }
    else if (selection == CONTROL_DECODER_SOURCE)
    {
        selection = CONTROL_NONE;
    }
    else
    {
        selection = (control_selection_t)((uint8_t)selection + 1u);
    }
    show_function_popup();
}

static void apply_encoder_step(int direction)
{
    switch (selection)
    {
    case CONTROL_MASTER:
    {
        int value = clamp_int(
            (int)front_panel_codec->headphone_volume_percent +
                5 * direction,
            0,
            100);
        (void)wm8960_set_headphone_volume(
            front_panel_codec,
            (uint8_t)value);
        (void)wm8960_set_speaker_volume(
            front_panel_codec,
            (uint8_t)value);
        break;
    }

    case CONTROL_SIDETONE:
    {
        int value = clamp_int(
            (int)audio_i2s_get_sidetone_volume() + 5 * direction,
            0,
            100);
        audio_i2s_set_sidetone_volume((uint8_t)value);
        break;
    }

    case CONTROL_FREQUENCY:
    {
        int value = clamp_int(
            (int)winkey_emulator_get_sidetone_frequency() +
                10 * direction,
            300,
            1200);
        winkey_emulator_set_sidetone_frequency((uint16_t)value);
        break;
    }

    case CONTROL_SPEED:
    {
        int value = clamp_int(
            (int)winkey_emulator_get_speed() + direction,
            5,
            99);
        winkey_emulator_set_speed((uint8_t)value);
        break;
    }

    case CONTROL_OUTPUT:
    {
        int value = (int)front_panel_codec->output + direction;
        if (value > (int)WM8960_OUTPUT_BOTH)
        {
            value = (int)WM8960_OUTPUT_HEADPHONES;
        }
        else if (value < (int)WM8960_OUTPUT_HEADPHONES)
        {
            value = (int)WM8960_OUTPUT_BOTH;
        }
        if (wm8960_set_output(
                front_panel_codec,
                (wm8960_output_t)value))
        {
            update_output_icons();
        }
        break;
    }

    case CONTROL_DECODER_SOURCE:
    {
        int value = (int)cw_display_source_get() + direction;
        if (value >= (int)CW_DISPLAY_SOURCE_COUNT)
        {
            value = 0;
        }
        else if (value < 0)
        {
            value = (int)CW_DISPLAY_SOURCE_COUNT - 1;
        }
        cw_display_source_set((cw_display_source_t)value);
        break;
    }

    case CONTROL_NONE:
    default:
        return;
    }
    show_function_popup();
}

bool front_panel_controls_init(wm8960_t *codec)
{
    if (codec == NULL)
    {
        return false;
    }
    front_panel_codec = codec;

    gpio_init(ENCODER_A_PIN);
    gpio_set_dir(ENCODER_A_PIN, GPIO_IN);
    gpio_pull_up(ENCODER_A_PIN);

    gpio_init(ENCODER_B_PIN);
    gpio_set_dir(ENCODER_B_PIN, GPIO_IN);
    gpio_pull_up(ENCODER_B_PIN);

    gpio_init(ENCODER_BUTTON_PIN);
    gpio_set_dir(ENCODER_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(ENCODER_BUTTON_PIN);

    encoder_history = (uint8_t)((gpio_get(ENCODER_A_PIN) ? 2u : 0u) |
                                (gpio_get(ENCODER_B_PIN) ? 1u : 0u));
    encoder_accumulator = 0;
    button_candidate = !gpio_get(ENCODER_BUTTON_PIN);
    button_pressed = button_candidate;
    button_candidate_since = to_ms_since_boot(get_absolute_time());
    selection = CONTROL_NONE;
    click_pending = false;
    awaiting_save_result = false;
    popup_visible = false;
    update_output_icons();
    return true;
}

void front_panel_controls_task(void)
{
    if (front_panel_codec == NULL)
    {
        return;
    }

    uint32_t now = to_ms_since_boot(get_absolute_time());
    handle_save_result();

    if (popup_visible && (int32_t)(now - popup_deadline_ms) >= 0)
    {
        hide_popup();
    }

    if (click_pending &&
        (uint32_t)(now - first_click_ms) > DOUBLE_CLICK_MS)
    {
        click_pending = false;
        select_next_function();
    }

    bool raw_button_pressed = !gpio_get(ENCODER_BUTTON_PIN);

    if (raw_button_pressed != button_candidate)
    {
        button_candidate = raw_button_pressed;
        button_candidate_since = now;
    }
    else if (button_candidate != button_pressed &&
             (uint32_t)(now - button_candidate_since) >=
                 BUTTON_DEBOUNCE_MS)
    {
        button_pressed = button_candidate;
        if (button_pressed)
        {
            if (awaiting_save_result)
            {
                click_pending = false;
            }
            else if (click_pending &&
                     (uint32_t)(now - first_click_ms) <= DOUBLE_CLICK_MS)
            {
                click_pending = false;
                save_on_double_click(now);
            }
            else
            {
                click_pending = true;
                first_click_ms = now;
            }
        }
    }

    uint8_t current = (uint8_t)((gpio_get(ENCODER_A_PIN) ? 2u : 0u) |
                                (gpio_get(ENCODER_B_PIN) ? 1u : 0u));
    encoder_history = (uint8_t)(((encoder_history << 2) | current) & 0x0fu);
    encoder_accumulator += encoder_transition[encoder_history];

    if (encoder_accumulator >= 4)
    {
        encoder_accumulator = 0;
        if (!settings_storage_is_busy())
        {
            apply_encoder_step(1);
        }
    }
    else if (encoder_accumulator <= -4)
    {
        encoder_accumulator = 0;
        if (!settings_storage_is_busy())
        {
            apply_encoder_step(-1);
        }
    }
}

void front_panel_controls_refresh(void)
{
    update_output_icons();
}
