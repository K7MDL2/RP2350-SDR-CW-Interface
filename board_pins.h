#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * Waveshare RP2350-PiZero 40Pin OUT mapping.
 *
 * The WM8960 Audio HAT occupies GPIO2, GPIO3, GPIO17, GPIO18, GPIO19,
 * GPIO20 and GPIO21.  Keep application controls off those pins.  Physical
 * pin numbers below refer to J5 in the RP2350-PiZero schematic.
 */
#define BOARD_I2C_SDA_PIN              2u  /* SDA.1 J5 pin 3  */
#define BOARD_I2C_SCL_PIN              3u  /* SCL.1 J5 pin 5  */
#define BOARD_ENCODER_A_PIN           27u  /* DT  was  15 GPIO.15 - J5 pin 29 marked as 5 on some pinouts*/
#define BOARD_ENCODER_B_PIN           26u  /* CLK was  6 GPIO.6 - J5 pin 31 */
#define BOARD_ENCODER_BUTTON_PIN      13u  /* was 13 GPIO.13 - J5 pin 33 */

#define LCD
//#define LED

#define BOARD_LED_MASTER_PIN           7u  /* was  7 GPIO.7 - J5 pin 26 */
#define BOARD_LED_SIDETONE_PIN        25u//27u  /* was  8 GPIO.8 - J5 pin 24 */
#define BOARD_LED_FREQUENCY_PIN        8u//27u  /* was  9 GPIO.9 - J5 pin 21 */
#define BOARD_LED_SPEED_PIN           16u  /* was 11 GPIO.11 - J5 pin 19 */
#define BOARD_LED_OUTPUT_PIN          12u  /* was 10 GPIO.10 - J5 pin 23 */
#define BOARD_LED_OUTPUT_HP_PIN       10u//26u  /* was 24 GPIO.24 - J5 pin 18 */
#define BOARD_LED_OUTPUT_SPK_PIN       9u//23u  /* was 25 GPIO.25 - J5 pin 20 */

#define BOARD_PADDLE_DIT_PIN          15u//14u  /* was 23 GPIO.23 - J5 pin 16 - 15 marked at 5 on some boards.*/
#define BOARD_PADDLE_DAH_PIN           6u//15u  /* was 27 GPIO.27 - J5 pin 13 */
#define BOARD_STRAIGHT_KEY_PIN        22u  /* was 22 GPIO.22 - J5 pin 15 */
#define BOARD_I2S_BCLK_PIN            18u  /* was 18 GPIO.18 - I2S_BCLK i2s frame clock input - J5 pin 12 */
#define BOARD_I2S_LRCLK_PIN           19u  /* was 19 GPIO.19 - CLK - I2S bit clock input -J5 pin 35 */
#define BOARD_I2S_ADC_DATA_PIN        20u  /* was 20 GPIO.20 - J5 pin 38 */
#define BOARD_I2S_DAC_DATA_PIN        21u  /* was 21 GPIO.21 - J5 pin 40 */

// for SeenGreat WM8960 Audio Codec hat with onboard 0.96" TFT display
// Amazon link https://www.amazon.com/gp/product/B0GK97XFJ3
// Wiki page at https://seengreat.com/wiki/206/
// Controller is ST7735 SPI.  backlight enable is GPIO.4
#define BOARD_TFT_DC_PIN          24u  /* Display register select/control (aka RS)*/
#define BOARD_TFT_CS_PIN           8u  /* CE0 Display chip select (aka CS or SPI1_CSn)*/
#define BOARD_TFT_RST_PIN         25u  /* Display reset (aka RESET or RES)*/
#define BOARD_TFT_BLK_PIN          4u  /* TFT Backlight (aka BLK)*/
#define BOARD_USER_BUTTON_PIN     17u  /* J5 pin 40 */
#define BOARD_SPI_MOSI_PIN        10u  /* SPI MOSI (aka SDA or SPI1_TX)*/
#define BOARD_SPI_MISO_PIN         9u  /* SPI MISO (not used)*/
#define BOARD_SPI_CLOCK_PIN       11u  /* SPI (SCLK) CLock (aka SCL or SPI1_SCK)*/
#define BOARD_I2C_SDA0_PIN         0u  /* GPIO.0 I2C 0 (SDA.0) - J5 pin 27 */
#define BOARD_I2C_SCL0_PIN         1u  /* GPIO.1 IC2 0 (SCL.0) - J5 pin 28 */


#endif
