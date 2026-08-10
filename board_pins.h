#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * Waveshare RP2350-PiZero 40Pin OUT mapping.
 *
 * The WM8960 Audio HAT occupies GPIO2, GPIO3, GPIO17, GPIO18, GPIO19,
 * GPIO20 and GPIO21.  Keep application controls off those pins.  Physical
 * pin numbers below refer to J5 in the RP2350-PiZero schematic.
 */
#define BOARD_I2C_SDA_PIN              2u  /* J5 pin 3  */
#define BOARD_I2C_SCL_PIN              3u  /* J5 pin 5  */
#define BOARD_ENCODER_A_PIN           15u  /* J5 pin 29 */
#define BOARD_ENCODER_B_PIN            6u  /* J5 pin 31 */
#define BOARD_ENCODER_BUTTON_PIN      13u  /* J5 pin 33 */
#define BOARD_LED_MASTER_PIN           7u  /* J5 pin 26 */
#define BOARD_LED_SIDETONE_PIN         8u  /* J5 pin 24 */
#define BOARD_LED_FREQUENCY_PIN       12u  /* J5 pin 21 */
#define BOARD_LED_SPEED_PIN           11u  /* J5 pin 19 */
#define BOARD_LED_OUTPUT_PIN          10u  /* J5 pin 23 */
#define BOARD_LED_OUTPUT_HP_PIN       24u  /* J5 pin 18 */
#define BOARD_LED_OUTPUT_SPK_PIN      25u  /* J5 pin 20 */
#define BOARD_PADDLE_DIT_PIN          23u  /* J5 pin 16 */
#define BOARD_PADDLE_DAH_PIN          27u  /* J5 pin 13 */
#define BOARD_STRAIGHT_KEY_PIN        22u  /* J5 pin 15 */
#define BOARD_I2S_BCLK_PIN            18u  /* J5 pin 12 */
#define BOARD_I2S_LRCLK_PIN           19u  /* J5 pin 35 */
#define BOARD_I2S_ADC_DATA_PIN        20u  /* J5 pin 38 */
#define BOARD_I2S_DAC_DATA_PIN        21u  /* J5 pin 40 */

#endif
