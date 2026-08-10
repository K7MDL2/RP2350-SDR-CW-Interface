
#include <stdio.h>
#include "wm8960.h"

#include <stddef.h>

/* WM8960 registers used by this playback-only driver. */
enum {
    WM8960_REG_LINVOL   = 0x00,
    WM8960_REG_RINVOL   = 0x01,
    WM8960_REG_LOUT1    = 0x02,
    WM8960_REG_ROUT1    = 0x03,
    WM8960_REG_CLOCK1   = 0x04,
    WM8960_REG_DACCTL1  = 0x05,
    WM8960_REG_IFACE1   = 0x07,
    WM8960_REG_CLOCK2   = 0x08,
    WM8960_REG_LDAC     = 0x0A,
    WM8960_REG_RDAC     = 0x0B,
    WM8960_REG_LADC     = 0x15,
    WM8960_REG_RADC     = 0x16,
    WM8960_REG_RESET    = 0x0F,
    WM8960_REG_POWER1   = 0x19,
    WM8960_REG_POWER2   = 0x1A,
    WM8960_REG_APOP1    = 0x1C,
    WM8960_REG_LINPATH  = 0x20,
    WM8960_REG_RINPATH  = 0x21,
    WM8960_REG_LOUTMIX  = 0x22,
    WM8960_REG_ROUTMIX  = 0x25,
    WM8960_REG_LOUT2    = 0x28,
    WM8960_REG_ROUT2    = 0x29,
    WM8960_REG_POWER3   = 0x2F,
    WM8960_REG_CLASSD1  = 0x31,
    WM8960_REG_PLL1     = 0x34,
    WM8960_REG_PLL2     = 0x35,
    WM8960_REG_PLL3     = 0x36,
    WM8960_REG_PLL4     = 0x37
};

/* R5 DACCTL1 */
#define WM8960_DAC_MUTE              (1u << 3)

/* R25 POWER1 */
#define WM8960_VMID_2X50K            (1u << 7)
#define WM8960_VMID_2X250K           (2u << 7)
#define WM8960_VREF                  (1u << 6)
#define WM8960_AINL                  (1u << 5)
#define WM8960_AINR                  (1u << 4)
#define WM8960_ADCL                  (1u << 3)
#define WM8960_ADCR                  (1u << 2)
#define WM8960_MICB                  (1u << 1)

/* R26 POWER2 */
#define WM8960_DACL                  (1u << 8)
#define WM8960_DACR                  (1u << 7)
#define WM8960_LOUT1_ENABLE          (1u << 6)
#define WM8960_ROUT1_ENABLE          (1u << 5)
#define WM8960_SPKL_ENABLE           (1u << 4)
#define WM8960_SPKR_ENABLE           (1u << 3)
#define WM8960_PLL_ENABLE            (1u << 0)

/* R28 APOP1 */
#define WM8960_POBCTRL               (1u << 7)
#define WM8960_BUFDCOPEN             (1u << 4)
#define WM8960_BUFIOEN               (1u << 3)
#define WM8960_SOFT_ST               (1u << 2)

/* R47 POWER3 */
#define WM8960_LOMIX_ENABLE          (1u << 3)
#define WM8960_ROMIX_ENABLE          (1u << 2)
#define WM8960_LIMIX_ENABLE          (1u << 5)
#define WM8960_RIMIX_ENABLE          (1u << 4)

/* R49 CLASSD1 */
#define WM8960_SPK_OP_EN_R           (1u << 7)
#define WM8960_SPK_OP_EN_L           (1u << 6)

/* R52 PLL1 */
#define WM8960_PLL_FRACTIONAL        (1u << 5)
#define WM8960_PLL_PRESCALE_DIV2     (1u << 4)

/*
 * Clocking A/B switch:
 *   true  -> use codec PLL
 *   false -> bypass PLL for an A/B check against the remaining soft tick
 */
#define WM8960_USE_PLL               false

/*
 * WM8960 runs as I2S slave. RP2350 PIO provides BCLK/LRCLK.
 */
#define WM8960_USE_MASTER_MODE       false

/*
 * Master IFACE1 presets for quick bring-up experiments:
 * 0x042 = normal BCLK/LRCLK
 * 0x0C2 = inverted BCLK
 * 0x052 = inverted LRCLK polarity
 * 0x0D2 = inverted BCLK + inverted LRCLK polarity
 */
#define WM8960_MASTER_IFACE1_VALUE   0x042u

/* Volume update bit in paired left/right volume registers. */
#define WM8960_VOLUME_UPDATE         (1u << 8)

static bool write_checked(wm8960_t *codec, uint8_t reg, uint16_t value)
{
    return wm8960_write_register(codec, reg, value);
}

void wm8960_bus_init(
    wm8960_t *codec,
    i2c_inst_t *i2c,
    uint sda_pin,
    uint scl_pin,
    uint32_t baudrate_hz)
{
    if (codec == NULL) {
        return;
    }

    codec->i2c = i2c;
    codec->sda_pin = sda_pin;
    codec->scl_pin = scl_pin;
    codec->i2c_baudrate = baudrate_hz;
    codec->address = WM8960_I2C_ADDRESS_7BIT;
    codec->output = WM8960_OUTPUT_HEADPHONES;
    codec->sample_rate = WM8960_RATE_48000;
    codec->bits_per_sample = 16;
    codec->headphone_volume_percent = 60;
    codec->speaker_volume_percent = 45;
    codec->initialized = false;
    codec->muted = true;
    codec->capturing = false;

    i2c_init(i2c, baudrate_hz);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);

    /*
     * The HAT normally already has I2C pull-ups. Enabling the weak internal
     * pulls is harmless and helps when testing with jumper wires.
     */
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
}

bool wm8960_write_register(wm8960_t *codec, uint8_t reg, uint16_t value)
{
    if (codec == NULL || codec->i2c == NULL || reg > 0x7F || value > 0x01FF) {
        return false;
    }

    /*
     * WM8960 control word:
     *
     * byte 0: register address in bits 7:1, value bit 8 in bit 0
     * byte 1: value bits 7:0
     */
    const uint8_t tx[2] = {
        (uint8_t)((reg << 1) | ((value >> 8) & 0x01u)),
        (uint8_t)(value & 0xFFu)
    };

    const int transferred = i2c_write_timeout_us(
        codec->i2c,
        codec->address,
        tx,
        sizeof(tx),
        false,
        5000
    );

    return transferred == (int)sizeof(tx);
}

static bool configure_pll(wm8960_t *codec, wm8960_sample_rate_t sample_rate)
{
    uint8_t pll_n;
    uint32_t pll_k;

    /*
     * HAT MCLK = 24 MHz.
     *
     * PLL input is prescaled to 12 MHz. The PLL output is configured to:
     *
     *   48 kHz  -> 24.5760 MHz, then SYSCLKDIV /2 -> 12.2880 MHz
     *   44.1kHz -> 22.5792 MHz, then SYSCLKDIV /2 -> 11.2896 MHz
     *
     * Internal SYSCLK is therefore 256 * sample_rate.
     */
    switch (sample_rate) {
        case WM8960_RATE_48000:
            pll_n = 8;
            pll_k = 0x3126E9u;
            break;

        case WM8960_RATE_44100:
            pll_n = 7;
            pll_k = 0x86C227u;
            break;

        default:
            return false;
    }

    /* Select MCLK temporarily and ensure the PLL is off while programming it. */
    if (!write_checked(codec, WM8960_REG_CLOCK1, 0x000)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_POWER2, 0x000)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_PLL2, (pll_k >> 16) & 0xFFu)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_PLL3, (pll_k >> 8) & 0xFFu)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_PLL4, pll_k & 0xFFu)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_PLL1,
            WM8960_PLL_FRACTIONAL |
            WM8960_PLL_PRESCALE_DIV2 |
            pll_n)) {
        return false;
    }

    /* Start PLL and allow it to lock. */
    if (!write_checked(codec, WM8960_REG_POWER2, WM8960_PLL_ENABLE)) {
        return false;
    }
    sleep_ms(250);

    /*
     * CLOCK1:
     * bit 0    CLKSEL=1: use PLL
     * bits 2:1 SYSCLKDIV=2
     * bits 5:3 DACDIV=1
     * bits 8:6 ADCDIV=1 (unused here)
     */
    return write_checked(codec, WM8960_REG_CLOCK1, 0x005);
}

static bool configure_i2s_format(
    wm8960_t *codec,
    uint8_t bits_per_sample,
    bool master_mode)
{
    uint16_t word_length;

    switch (bits_per_sample) {
        case 16:
            word_length = 0x000;
            break;
        case 20:
            word_length = 0x004;
            break;
        case 24:
            word_length = 0x008;
            break;
        case 32:
            word_length = 0x00C;
            break;
        default:
            return false;
    }

    /*
     * IFACE1 / R7:
     *
     * bit 6    MS: 0=slave, 1=master
     * bits 3:2 word length
     * bits 1:0 FORMAT=10: I2S
     * normal BCLK and LRCLK polarity
     */
    uint16_t value = word_length | 0x002u;

    if (master_mode) {
        value |= 0x040u;
    }

    return write_checked(codec, WM8960_REG_IFACE1, value);
}


static bool enable_i2s_master_clocks(wm8960_t *codec)
{
    if (!WM8960_USE_MASTER_MODE) {
        return true;
    }

    printf("WM8960: writing CLOCK2 R8 = 0x1C7\r\n");

    if (!write_checked(
            codec,
            WM8960_REG_CLOCK2,
            0x1C7u)) {

        printf("WM8960: CLOCK2 write failed\r\n");
        return false;
    }

    printf(
        "WM8960: writing IFACE1 R7 = 0x%03X master I2S\r\n",
        WM8960_MASTER_IFACE1_VALUE
    );

    if (!write_checked(
            codec,
            WM8960_REG_IFACE1,
            WM8960_MASTER_IFACE1_VALUE)) {

        printf("WM8960: IFACE1 master write failed\r\n");
        return false;
    }

    printf("WM8960: master clock registers written\r\n");
    return true;
}


static uint16_t output_power2_value(wm8960_output_t output)
{
    uint16_t value = WM8960_DACL | WM8960_DACR;

    if (WM8960_USE_PLL) {
        value |= WM8960_PLL_ENABLE;
    }

    if ((output & WM8960_OUTPUT_HEADPHONES) != 0) {
        value |= WM8960_LOUT1_ENABLE | WM8960_ROUT1_ENABLE;
    }

    if ((output & WM8960_OUTPUT_SPEAKERS) != 0) {
        value |= WM8960_SPKL_ENABLE | WM8960_SPKR_ENABLE;
    }

    return value;
}

static uint16_t output_classd_value(wm8960_output_t output)
{
    if ((output & WM8960_OUTPUT_SPEAKERS) != 0) {
        return WM8960_SPK_OP_EN_L | WM8960_SPK_OP_EN_R;
    }

    return 0;
}

static uint8_t percent_to_output_volume(uint8_t percent)
{
    if (percent == 0) {
        return 0x00u;
    }

    if (percent > 100) {
        percent = 100;
    }

    /*
     * Use the practical output range from roughly -40 dB to 0 dB.
     *
     *   1%   -> 0x51, approximately -40 dB
     *   100% -> 0x79, approximately   0 dB
     */
    const uint32_t minimum = 0x51u;
    const uint32_t maximum = 0x79u;

    return (uint8_t)(
        minimum +
        (((uint32_t)(percent - 1u) * (maximum - minimum)) / 99u)
    );
}

bool wm8960_set_mute(wm8960_t *codec, bool mute)
{
    if (codec == NULL || !codec->initialized) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_DACCTL1,
            mute ? WM8960_DAC_MUTE : 0x000)) {
        return false;
    }

    codec->muted = mute;
    return true;
}

bool wm8960_set_headphone_volume(
    wm8960_t *codec,
    uint8_t percent)
{
    if (codec == NULL ||
        !codec->initialized ||
        percent > 100) {
        return false;
    }

    const uint16_t volume =
        percent_to_output_volume(percent);

    /*
     * Links zonder updatebit naar de tussenlatch.
     * Rechts met VU=1 werkt beide kanalen tegelijk bij.
     */
    if (!write_checked(
            codec,
            WM8960_REG_LOUT1,
            volume)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_ROUT1,
            WM8960_VOLUME_UPDATE | volume)) {
        return false;
    }

    codec->headphone_volume_percent = percent;
    return true;
}

bool wm8960_set_speaker_volume(
    wm8960_t *codec,
    uint8_t percent)
{
    if (codec == NULL ||
        !codec->initialized ||
        percent > 100) {
        return false;
    }

    const uint16_t volume =
        percent_to_output_volume(percent);

    if (!write_checked(
            codec,
            WM8960_REG_LOUT2,
            volume)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_ROUT2,
            WM8960_VOLUME_UPDATE | volume)) {
        return false;
    }

    codec->speaker_volume_percent = percent;
    return true;
}

bool wm8960_set_output(wm8960_t *codec, wm8960_output_t output)
{
    if (codec == NULL || !codec->initialized ||
        output < WM8960_OUTPUT_HEADPHONES ||
        output > WM8960_OUTPUT_BOTH) {
        return false;
    }

    const bool was_muted = codec->muted;

    if (!was_muted && !wm8960_set_mute(codec, true)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_CLASSD1, output_classd_value(output))) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_POWER2, output_power2_value(output))) {
        return false;
    }

    codec->output = output;
    sleep_ms(10);

    if (!was_muted && !wm8960_set_mute(codec, false)) {
        return false;
    }

    return true;
}

bool wm8960_init_playback(
    wm8960_t *codec,
    wm8960_sample_rate_t sample_rate,
    uint8_t bits_per_sample,
    wm8960_output_t output)
{
    if (codec == NULL || codec->i2c == NULL ||
        output < WM8960_OUTPUT_HEADPHONES ||
        output > WM8960_OUTPUT_BOTH) {
        return false;
    }

    codec->initialized = false;
    codec->muted = true;
    codec->sample_rate = sample_rate;
    codec->bits_per_sample = bits_per_sample;
    codec->output = output;

    printf(
        "WM8960: I2S mode = %s\r\n",
        WM8960_USE_MASTER_MODE ? "master" : "slave"
    );

    /* Any valid write also confirms that the slave acknowledged address 0x1A. */
    if (!write_checked(codec, WM8960_REG_RESET, 0x000)) {
        return false;
    }
    sleep_ms(5);

    /* Keep the DAC muted throughout clock and analogue power-up. */
    if (!write_checked(codec, WM8960_REG_DACCTL1, WM8960_DAC_MUTE)) {
        return false;
    }

    /*
     * Follow the Linux driver bias ramp more closely:
     *   1. enable anti-pop helpers
     *   2. ramp VMID at 2x50k
     *   3. enable VREF
     *   4. drop back to steady-state VMID 2x250k with BUFIOEN left enabled
     */
    if (!write_checked(
            codec,
            WM8960_REG_APOP1,
            WM8960_POBCTRL |
            WM8960_SOFT_ST |
            WM8960_BUFDCOPEN |
            WM8960_BUFIOEN)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_POWER1,
            WM8960_VMID_2X50K)) {
        return false;
    }

    sleep_ms(100);

    if (!write_checked(
            codec,
            WM8960_REG_POWER1,
            WM8960_VMID_2X50K | WM8960_VREF)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_APOP1, WM8960_BUFIOEN)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_POWER1,
            WM8960_VMID_2X250K | WM8960_VREF)) {
        return false;
    }

    sleep_ms(50);

    if (WM8960_USE_PLL) {
        if (!configure_pll(codec, sample_rate)) {
            return false;
        }
        printf("WM8960: clock source = PLL\r\n");
    } else {
        /*
         * Bypass PLL and use MCLK directly.
         * CLOCK1 = 0x000 keeps CLKSEL=MCLK and all dividers at default /1.
         */
        if (!write_checked(codec, WM8960_REG_CLOCK1, 0x000)) {
            return false;
        }

        printf("WM8960: clock source = MCLK (PLL bypass)\r\n");
    }

    if (WM8960_USE_MASTER_MODE) {
        if (!enable_i2s_master_clocks(codec)) {
            return false;
        }
    } else {
        if (!configure_i2s_format(codec, bits_per_sample, false)) {
            return false;
        }
    }

    /*
     * Digital DAC volume at 0 dB.
     *
     * Write left without VU first. Writing right with VU then applies both
     * channel values simultaneously.
     */
    if (!write_checked(codec, WM8960_REG_LDAC, 0x0FF)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_RDAC, 0x1FF)) {
        return false;
    }

    /*
     * Route Left DAC -> Left Output Mixer and Right DAC -> Right Output Mixer.
     */
    if (!write_checked(codec, WM8960_REG_LOUTMIX, 0x100)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_ROUTMIX, 0x100)) {
        return false;
    }

    /* Power both output mixers. */
    if (!write_checked(
            codec,
            WM8960_REG_POWER3,
            WM8960_LOMIX_ENABLE | WM8960_ROMIX_ENABLE)) {
        return false;
    }

    /*
     * Enable the requested analogue output paths while retaining DAC and PLL.
     */
    if (!write_checked(codec, WM8960_REG_CLASSD1, output_classd_value(output))) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_POWER2, output_power2_value(output))) {
        return false;
    }

    codec->initialized = true;

    /* Conservative startup volumes. */
    if (!wm8960_set_headphone_volume(codec, codec->headphone_volume_percent)) {
        codec->initialized = false;
        return false;
    }
    if (!wm8960_set_speaker_volume(codec, codec->speaker_volume_percent)) {
        codec->initialized = false;
        return false;
    }

    /*
     * Deliberately remain muted. Start BCLK/LRCLK/data on the RP2350 before
     * calling wm8960_start_playback().
     */
    codec->muted = true;
    return true;
}

bool wm8960_start_playback(wm8960_t *codec)
{
    if (codec == NULL || !codec->initialized) {
        return false;
    }

    if (WM8960_USE_MASTER_MODE) {
        /*
         * WM8960 is already configured in I2S master mode during init. Ensure
         * BCLK/LRCLK are running for several frames before enabling analogue
         * path.
         */
        sleep_ms(5);
    }

    /*
     * Allow several externally generated BCLK/LRCLK frames to pass before
     * enabling the analogue playback path.
     */
    sleep_ms(5);

    /*
     * Re-apply the verified playback path after I2S is active. This matches
     * the direct register test that produced audible output on the HAT.
     */

    /* Digital DAC volume at 0 dB, with paired-channel update. */
    if (!write_checked(codec, WM8960_REG_LDAC, 0x0FF)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_RDAC, 0x1FF)) {
        return false;
    }

    /* Route left/right DACs to their output mixers. */
    if (!write_checked(codec, WM8960_REG_LOUTMIX, 0x100)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_ROUTMIX, 0x100)) {
        return false;
    }

    /* Power both output mixers. */
    if (!write_checked(
            codec,
            WM8960_REG_POWER3,
            WM8960_LOMIX_ENABLE | WM8960_ROMIX_ENABLE)) {
        return false;
    }

    /* Power both DACs, the selected outputs and the PLL. */
    if (!write_checked(
            codec,
            WM8960_REG_POWER2,
            output_power2_value(codec->output))) {
        return false;
    }

    /*
     * Re-apply volume after powering the analogue output path.
     */
    if ((codec->output & WM8960_OUTPUT_HEADPHONES) != 0) {
        if (!wm8960_set_headphone_volume(
                codec,
                codec->headphone_volume_percent)) {
            return false;
        }
    }

    if ((codec->output & WM8960_OUTPUT_SPEAKERS) != 0) {
        if (!wm8960_set_speaker_volume(
                codec,
                codec->speaker_volume_percent)) {
            return false;
        }
    }

    sleep_ms(5);

    /*
     * Startup used low-impedance VMID for fast bias settling.
     * Switch to normal playback VMID to minimize analog bias modulation.
     */
    if (!write_checked(
            codec,
            WM8960_REG_POWER1,
            WM8960_VMID_2X250K | WM8960_VREF)) {
        return false;
    }

    sleep_ms(5);

    /*
     * Keep only the lighter steady-state buffer path enabled, matching the
     * Linux driver standby state after VMID/VREF have settled.
     */
    if (!write_checked(codec, WM8960_REG_APOP1, WM8960_BUFIOEN)) {
        return false;
    }

    /* Digital DAC unmute. */
    return wm8960_set_mute(codec, false);
}

bool wm8960_start_capture(wm8960_t *codec)
{
    if (codec == NULL || !codec->initialized) {
        return false;
    }

    /* LINPUT1/RINPUT1 -> boost mixers, +29 dB boost, then input mixers. */
    if (!write_checked(codec, WM8960_REG_LINPATH, 0x138)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_RINPATH, 0x138)) {
        return false;
    }

    /* Analogue input PGA: approximately +6.75 dB, unmuted. */
    if (!write_checked(codec, WM8960_REG_LINVOL, 0x020)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_RINVOL, 0x120)) {
        return false;
    }

    /* ADC digital gain at 0 dB, paired update on the right channel. */
    if (!write_checked(codec, WM8960_REG_LADC, 0x0C3)) {
        return false;
    }
    if (!write_checked(codec, WM8960_REG_RADC, 0x1C3)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_POWER3,
            WM8960_LIMIX_ENABLE | WM8960_RIMIX_ENABLE |
            WM8960_LOMIX_ENABLE | WM8960_ROMIX_ENABLE)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_POWER1,
            WM8960_VMID_2X250K | WM8960_VREF |
            WM8960_AINL | WM8960_AINR |
            WM8960_ADCL | WM8960_ADCR | WM8960_MICB)) {
        return false;
    }

    sleep_ms(100);
    codec->capturing = true;
    return true;
}

bool wm8960_stop_playback(wm8960_t *codec)
{
    return wm8960_set_mute(codec, true);
}

bool wm8960_power_down(wm8960_t *codec)
{
    if (codec == NULL || !codec->initialized) {
        return false;
    }

    if (!wm8960_set_mute(codec, true)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_CLASSD1, 0x000)) {
        return false;
    }

    /* Power down DACs, headphones, speakers and PLL. */
    if (!write_checked(codec, WM8960_REG_POWER2, 0x000)) {
        return false;
    }

    if (!write_checked(
            codec,
            WM8960_REG_APOP1,
            WM8960_POBCTRL |
            WM8960_SOFT_ST |
            WM8960_BUFDCOPEN |
            WM8960_BUFIOEN)) {
        return false;
    }

    if (!write_checked(codec, WM8960_REG_POWER1, 0x000)) {
        return false;
    }

    sleep_ms(600);
    codec->initialized = false;
    codec->capturing = false;
    return true;
}
