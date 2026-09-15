#include "front_panel_controls.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_i2s_test.h"
#include "board_pins.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "settings_storage.h"
#include "winkey_emulator.h"
#include "lcd_extra.h"
#include "lcd.h"

#define ENCODER_A_PIN BOARD_ENCODER_A_PIN
#define ENCODER_B_PIN BOARD_ENCODER_B_PIN
#define ENCODER_BUTTON_PIN BOARD_ENCODER_BUTTON_PIN

#define LED_MASTER_PIN BOARD_LED_MASTER_PIN
#define LED_SIDETONE_PIN BOARD_LED_SIDETONE_PIN
#define LED_FREQUENCY_PIN BOARD_LED_FREQUENCY_PIN
#define LED_SPEED_PIN BOARD_LED_SPEED_PIN
#define LED_OUTPUT_PIN BOARD_LED_OUTPUT_PIN
#define LED_OUTPUT_HP_PIN BOARD_LED_OUTPUT_HP_PIN
#define LED_OUTPUT_SPK_PIN BOARD_LED_OUTPUT_SPK_PIN

#define BUTTON_DEBOUNCE_MS 20u
#define DOUBLE_CLICK_MS 400u
#define SAVE_LED_PULSE_MS 150u
#define ERROR_LED_PULSE_MS 100u

typedef enum
{
    CONTROL_MASTER = 0,
    CONTROL_SIDETONE,
    CONTROL_FREQUENCY,
    CONTROL_SPEED,
    CONTROL_OUTPUT,
    CONTROL_NONE
} control_selection_t;

//#ifdef LED
static const uint8_t function_led_pins[] = {
    LED_MASTER_PIN,
    LED_SIDETONE_PIN,
    LED_FREQUENCY_PIN,
    LED_SPEED_PIN,
    LED_OUTPUT_PIN};
//#endif

static const uint8_t all_led_pins[] = {
    LED_MASTER_PIN,
    LED_SIDETONE_PIN,
    LED_FREQUENCY_PIN,
    LED_SPEED_PIN,
    LED_OUTPUT_PIN,
    LED_OUTPUT_HP_PIN,
    LED_OUTPUT_SPK_PIN};

// Using row and column is useful to allow easier scaling to different resolution screens.  
struct led_location_t {
    uint16_t x; // x position of left edge of LED icon
    uint16_t row;   // row (used with height (row * height)
    uint16_t row_height; // set based on font and visual style goals.
    uint16_t w; // LED icon width
    uint16_t h; // LED icon height
    uint16_t r; // radius if rounded rectangle or circle used.
    uint16_t s; // LED icon padding v and h
    uint16_t on_color;  // when control is active use this color
    uint16_t off_color;  // when control is inactive, use this color
    char LED_label;  // can be used to draw a single char label inside the functon LED icon.
    char label[160];  // 1 fullwidth buffer for 1 line of text.  Normall shorter for active function label confirmation
};

// for 80x160px display
const uint16_t RW = 3;  // row
const uint16_t RH = 16; // row height
const uint16_t w  = 14; // icon width
const uint16_t h  = 18; // icon height
const uint16_t r  = 3;  // radius of a rounded rectangle or circle
const uint16_t s  = 8;  // icon spacing. (CW-w) will have some sdpacing already so this is additional padding h and v

#define NUM_LEDS 7

struct led_location_t led_pin_location[NUM_LEDS] = {
    {10, RW, RH, w, h, r, s, GREEN, LGRAY, 'M', "Master Volume"}, //LED_MASTER_PIN,
    {10, RW, RH, w, h, r, s, GREEN, LGRAY, 'L', "Sidetone Level"}, //LED_SIDETONE_PIN
    {10, RW, RH, w, h, r, s, GREEN, LGRAY, 'F', "Sidetone Frequency"}, //LED_FREQUENCY_PIN
    {10, RW, RH, w, h, r, s, GREEN, LGRAY, 'S', "Speed"}, //LED_SPEED_PIN
    {10, RW, RH, w, h, r, s, GREEN, LGRAY, 'O', "Output Select (HP/Spkr/Both)"}, //LED_OUTPUT_PIN
    {10, RW, RH, w, h, r, s, BLUE, LGRAY, 'H', "HP"}, //LED_OUTPUT_HP_PIN
    {10, RW, RH, w, h, r, s, BLUE, LGRAY, 'S', "SPKR"}  //LED_OUTPUT_SPK_PIN
};

// 1st col  -  (26+0+((18-14))/2) = 28 left edge for 14px wide LED color square 

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
static bool feedback_active;
static bool feedback_leds_on;
static uint8_t feedback_transitions_remaining;
static uint32_t feedback_deadline_ms;
static uint32_t feedback_interval_ms;

/******************************************************************************
    Function description: Draw or update a "LED" images on screen based on ID
******************************************************************************/

void LCD_Draw_LED(uint16_t x,uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    LCD_Fill(x-w/2,y-h/2,x+w/2,y+h/2,color);
}

// Update a single LED text icon
void LCD_update_n_LED(uint8_t led_id, bool state) {
    static uint8_t last_states;
    static uint8_t valid_states;
    uint8_t state_mask = (uint8_t)(1u << led_id);

    if ((valid_states & state_mask) != 0u &&
        (((last_states & state_mask) != 0u) == state)) {
        return;
    }
    if (state) {
        last_states |= state_mask;
    } else {
        last_states &= (uint8_t)~state_mask;
    }
    valid_states |= state_mask;

    uint16_t w = led_pin_location[led_id].w;
    uint16_t h = led_pin_location[led_id].h;
    uint16_t s = led_pin_location[led_id].s;
    uint16_t x = led_pin_location[led_id].x;
    uint16_t row = led_pin_location[led_id].row;
    uint16_t rh = led_pin_location[led_id].row_height;
    //uint16_t r = led_pin_location[led_id].radius;
    uint16_t color;

    // compute the icon location with each grid column and row.  Use to pad more spacing up and down.
    uint16_t x1 = x + led_id*w + led_id*s;    // calc space between the icon and col. take 1/2 and add to the left side for x
    // 1st col  -  5+0*10 + 0*8 = 5  // must be > w/2
    // 2nd col  -  5+1*10 + 1*8 = 23
    // 3rd col  -  5+2*10 + 2*8 = 41
    // 4th col  -  5+3*10 + 3*8 = 59 
    // 5th col  -  5+4*10 + 4*8 = 77
    // 6th col  -  5+5*10 + 5*8 = 95
    // 7th col  -  5+6*10 + 6*8 = 113
    uint16_t y = row*rh + h + s;   // col width x col height, add spacing

    static uint8_t last_led_id = 0;
    char temp_str[160] = "";

    if (state) {
        color = led_pin_location[led_id].on_color;
        
        // ToDo: Draw active function label above LED icons.  erase before and after timeout.   Append real time value from encoder for this function
        strcpy(temp_str, led_pin_location[last_led_id].label);    // erase old string
        //LCD_ShowString(8, 3*16, (uint8_t *) temp_str, BLACK);
        //LCD_ShowStringLn(x, 0*rh, 0*8, 17*8, (const u8 *)temp_str, 1, BLACK);
        
        strcpy(temp_str, led_pin_location[led_id].label);
        //LCD_ShowString(8, 3*16, (uint8_t *) temp_str, CYAN);
        //LCD_ShowStringLn(x, 0*rh, 0*8, 17*8, (const u8 *)temp_str, 1, CYAN);
        last_led_id = led_id;
    }
    else {
        color = led_pin_location[led_id].off_color;
    }
    
    LCD_Draw_LED(x1, y, w, h, color);  // w/2 = radius of circle or half width.  Draw takes radio so will do w*2
    LCD_ShowChar(x1-4, (4*rh), led_pin_location[led_id].LED_label, 1, BLACK);  // overlay icon with char label
}

void LCD_update_all_LEDs(bool state) {
    for (int i=0; i< NUM_LEDS; i++)
    {
        LCD_update_n_LED(i, state);
    }
}

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

static void set_all_leds(bool on)
{
    for (size_t index = 0u;
         index < sizeof(all_led_pins) / sizeof(all_led_pins[0]);
         ++index) {
        #ifdef LEDS
        gpio_put(all_led_pins[index], on);
        #endif
        #ifdef LCD
        LCD_update_n_LED(index, on);
        #endif
    }
}

static void update_selection_leds(void)
{
    
    if (feedback_active || awaiting_save_result)
    {
        return;
    }
    for (size_t index = 0u;
         index < sizeof(function_led_pins) / sizeof(function_led_pins[0]);
         ++index)
    {
        #ifdef LEDS
        gpio_put(
            function_led_pins[index],
            selection == (control_selection_t)index);   
        gpio_put(
            LED_OUTPUT_HP_PIN,
            (front_panel_codec->output & WM8960_OUTPUT_HEADPHONES) != 0);
        gpio_put(
            LED_OUTPUT_SPK_PIN,
            (front_panel_codec->output & WM8960_OUTPUT_SPEAKERS) != 0);
        #endif
        
        #ifdef LCD        
        // update the function icons and text
        LCD_update_n_LED(index, selection == (control_selection_t)index);  // selection is 0 or 1 based on match or not
        #endif
    }

            #ifdef LCD
            // Update output indicators once, after the function indicators.
            LCD_update_n_LED(NUM_LEDS-2, (front_panel_codec->output & WM8960_OUTPUT_HEADPHONES) != 0);
            LCD_update_n_LED(NUM_LEDS-1, (front_panel_codec->output & WM8960_OUTPUT_SPEAKERS) != 0);
            #endif
}

static void start_led_feedback(
    uint8_t pulse_count,
    uint32_t interval_ms,
    uint32_t now)
{
    feedback_active = true;
    feedback_leds_on = true;
    feedback_transitions_remaining = (uint8_t)(pulse_count * 2u);
    feedback_interval_ms = interval_ms;
    feedback_deadline_ms = now + interval_ms;
    set_all_leds(true);
}

static void save_on_double_click(uint32_t now)
{
    settings_save_request_t request = settings_storage_request_save();
    switch (request)
    {
    case SETTINGS_SAVE_ACCEPTED:
        awaiting_save_result = true;
        set_all_leds(true);
        break;
    case SETTINGS_SAVE_UNCHANGED:
        start_led_feedback(1u, SAVE_LED_PULSE_MS, now);
        break;
    case SETTINGS_SAVE_BUSY:
    case SETTINGS_SAVE_UNAVAILABLE:
    default:
        start_led_feedback(3u, ERROR_LED_PULSE_MS, now);
        break;
    }
}

static void handle_led_feedback(uint32_t now)
{
    settings_save_result_t result = settings_storage_take_result();
    if (result != SETTINGS_SAVE_RESULT_NONE)
    {
        awaiting_save_result = false;
        if (result == SETTINGS_SAVE_RESULT_OK)
        {
            start_led_feedback(1u, SAVE_LED_PULSE_MS, now);
        }
        else
        {
            start_led_feedback(3u, ERROR_LED_PULSE_MS, now);
        }
    }

    if (!feedback_active || (int32_t)(now - feedback_deadline_ms) < 0)
    {
        return;
    }

    feedback_leds_on = !feedback_leds_on;
    set_all_leds(feedback_leds_on);
    if (feedback_transitions_remaining > 0u)
    {
        --feedback_transitions_remaining;
    }
    if (feedback_transitions_remaining == 0u)
    {
        feedback_active = false;
        feedback_leds_on = false;
        update_selection_leds();
    }
    else
    {
        feedback_deadline_ms = now + feedback_interval_ms;
    }
}

static void select_next_function(void)
{
    if (selection == CONTROL_NONE)
    {
        selection = CONTROL_MASTER;
    }
    else if (selection == CONTROL_OUTPUT)
    {
        selection = CONTROL_NONE;
    }
    else
    {
        selection = (control_selection_t)((uint8_t)selection + 1u);
    }
    update_selection_leds();
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
            update_selection_leds();
        }
        break;
    }

    case CONTROL_NONE:
    default:
        break;
    }
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

    #ifdef LEDS
    for (size_t index = 0u;
         index < sizeof(all_led_pins) / sizeof(all_led_pins[0]);
         ++index)
    {
        gpio_init(all_led_pins[index]);
        gpio_set_dir(all_led_pins[index], GPIO_OUT);
        gpio_put(all_led_pins[index], false);
    }
    #endif
    #ifdef LCD
    set_all_leds(1);
    sleep_ms(700);
    set_all_leds(0);
    #endif
    
    encoder_history = (uint8_t)((gpio_get(ENCODER_A_PIN) ? 2u : 0u) |
                                (gpio_get(ENCODER_B_PIN) ? 1u : 0u));
    encoder_accumulator = 0;
    button_candidate = !gpio_get(ENCODER_BUTTON_PIN);
    button_pressed = button_candidate;
    button_candidate_since = to_ms_since_boot(get_absolute_time());
    selection = CONTROL_NONE;
    click_pending = false;
    awaiting_save_result = false;
    feedback_active = false;
    update_selection_leds();
    return true;
}

void front_panel_controls_task(void)
{
    if (front_panel_codec == NULL)
    {
        return;
    }

    uint32_t now = to_ms_since_boot(get_absolute_time());
    handle_led_feedback(now);

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
            if (awaiting_save_result || feedback_active)
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
        if (!settings_storage_is_busy() && !feedback_active)
        {
            apply_encoder_step(1);
        }
    }
    else if (encoder_accumulator <= -4)
    {
        encoder_accumulator = 0;
        if (!settings_storage_is_busy() && !feedback_active)
        {
            apply_encoder_step(-1);
        }
    }
}

void front_panel_controls_refresh(void)
{
    update_selection_leds();
}
