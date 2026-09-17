#include "cw_decoder.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "control_console.h"
#include "cw_text_display.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#define INPUT_RATE_HZ 48000u
#define DECODER_RATE_HZ 8000u
#define DECIMATION (INPUT_RATE_HZ / DECODER_RATE_HZ)
#define WINDOW_SAMPLES 80u
#define WINDOW_HOP_SAMPLES 40u
#define WINDOW_STEP_MS 5u
#define SAMPLE_RING_SIZE 2048u
#define SAMPLE_RING_MASK (SAMPLE_RING_SIZE - 1u)
#define EVENT_RING_SIZE 64u
#define EVENT_RING_MASK (EVENT_RING_SIZE - 1u)
#define TIMING_HISTORY_SIZE 20u

/* Boosts quiet RX audio and paddle sidetone above the detection threshold. */
#define INPUT_GAIN 4

_Static_assert((SAMPLE_RING_SIZE & SAMPLE_RING_MASK) == 0u,
               "sample ring must be a power of two");
_Static_assert((EVENT_RING_SIZE & EVENT_RING_MASK) == 0u,
               "event ring must be a power of two");
_Static_assert(INPUT_RATE_HZ % DECODER_RATE_HZ == 0u,
               "decoder decimation must be integral");

static int16_t sample_ring[SAMPLE_RING_SIZE];
static volatile uint32_t sample_write;
static volatile uint32_t sample_read;
static char event_ring[EVENT_RING_SIZE];
static volatile uint32_t event_write;
static volatile uint32_t event_read;
static volatile uint32_t dropped_samples;
static volatile bool decoder_enabled = false;
static volatile uint16_t requested_frequency_hz = 750u;
static volatile uint8_t estimated_wpm = 20u;
static volatile uint8_t fixed_wpm;
static uint8_t decimation_count;
static int32_t decimation_sum;
static volatile float last_signal_amplitude;
static volatile float last_signal_threshold;

static const struct {
    const char *pattern;
    char character;
} morse_table[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
    {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
    {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
    {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'}, {".----", '1'}, {"..---", '2'},
    {"...--", '3'}, {"....-", '4'}, {".....", '5'}, {"-....", '6'},
    {"--...", '7'}, {"---..", '8'}, {"----.", '9'}, {"-----", '0'},
    {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-..-.", '/'},
    {"-....-", '-'}, {".-.-.", '+'}, {"-...-", '='}, {"---...", ':'},
    {"-.-.-.", ';'}, {"-.-.--", '!'}, {".----.", '\''}, {".-..-.", '"'},
    {"-.--.", '('}, {"-.--.-", ')'}, {".--.-.", '@'}, {"..--.-", '_'},
    {".-...", '&'}
};

static void queue_event(char character)
{
    uint32_t write = event_write;
    uint32_t next = (write + 1u) & EVENT_RING_MASK;
    if (next == event_read) {
        return;
    }
    event_ring[write] = character;
    __dmb();
    event_write = next;
}

static char decode_pattern(const char *pattern)
{
    for (size_t i = 0u; i < sizeof(morse_table) / sizeof(morse_table[0]); ++i) {
        if (strcmp(pattern, morse_table[i].pattern) == 0) {
            return morse_table[i].character;
        }
    }
    return '#';
}

static float update_dot_estimate(
    uint16_t history[TIMING_HISTORY_SIZE],
    size_t *history_count,
    size_t *history_write,
    uint16_t duration_ms,
    float current_dot_ms)
{
    history[*history_write] = duration_ms;
    *history_write = (*history_write + 1u) % TIMING_HISTORY_SIZE;
    if (*history_count < TIMING_HISTORY_SIZE) {
        ++*history_count;
    }
    if (*history_count < 4u) {
        return current_dot_ms;
    }

    uint16_t sorted[TIMING_HISTORY_SIZE];
    memcpy(sorted, history, *history_count * sizeof(sorted[0]));
    for (size_t i = 1u; i < *history_count; ++i) {
        uint16_t value = sorted[i];
        size_t j = i;
        while (j != 0u && sorted[j - 1u] > value) {
            sorted[j] = sorted[j - 1u];
            --j;
        }
        sorted[j] = value;
    }

    /*
     * DITs normally form the lower part of the mark-duration distribution.
     * The 25th percentile ignores isolated short clicks, unlike a permanent
     * minimum, while still selecting DITs in ordinary text.
     */
    float candidate = (float)sorted[*history_count / 4u];

    /* Do not let one or two short drop-outs pull the speed upward. */
    size_t supporting_marks = 0u;
    for (size_t i = 0u; i < *history_count; ++i) {
        float measured = (float)sorted[i];
        if (measured >= 0.80f * candidate &&
            measured <= 1.20f * candidate) {
            ++supporting_marks;
        }
    }
    if (supporting_marks < 3u) {
        return current_dot_ms;
    }

    /* Maximum timing movement is 1% per accepted mark. */
    float lower_limit = current_dot_ms * 0.90f;
    float upper_limit = current_dot_ms * 1.10f;
    if (candidate < lower_limit) candidate = lower_limit;
    if (candidate > upper_limit) candidate = upper_limit;
    return 0.90f * current_dot_ms + 0.10f * candidate;
}

static void decoder_core(void)
{
    multicore_lockout_victim_init();

    float coefficient = 0.0f;
    uint16_t active_frequency = 0u;
    float noise_amplitude = 100.0f;
    float dot_ms = 60.0f;
    uint16_t timing_history[TIMING_HISTORY_SIZE] = {0};
    size_t timing_history_count = 0u;
    size_t timing_history_write = 0u;
    bool tone = false;
    bool candidate = false;
    uint8_t candidate_count = 0u;
    uint32_t state_ms = 0u;
    bool character_sent = false;
    bool word_sent = false;
    bool stream_active = false;
    bool stream_end_sent = false;
    char pattern[8] = {0};
    size_t pattern_length = 0u;
    int16_t window[WINDOW_SAMPLES];
    size_t window_count = 0u;

    while (true) {
        /*
         * Runs on core1 so all LCD/SPI drawing stays off core0, which must
         * stay free to service the time-critical audio DMA ring.
         */
        cw_text_display_task();

        if (!decoder_enabled) {
            sample_read = sample_write;
            window_count = 0u;
            tone = false;
            pattern_length = 0u;
            state_ms = 0u;
            timing_history_count = 0u;
            timing_history_write = 0u;
            dot_ms = fixed_wpm != 0u ? 1200.0f / (float)fixed_wpm : 60.0f;
            estimated_wpm = fixed_wpm != 0u ? fixed_wpm : 20u;
            sleep_ms(1u);
            continue;
        }

        uint32_t read = sample_read;
        if (read == sample_write) {
            tight_loop_contents();
            continue;
        }
        window[window_count++] = sample_ring[read];
        __dmb();
        sample_read = (read + 1u) & SAMPLE_RING_MASK;
        if (window_count != WINDOW_SAMPLES) {
            continue;
        }
        uint16_t frequency = requested_frequency_hz;
        if (frequency != active_frequency) {
            coefficient = 2.0f * cosf(
                2.0f * (float)M_PI * (float)frequency /
                (float)DECODER_RATE_HZ
            );
            active_frequency = frequency;
            noise_amplitude = 100.0f;
        }

        uint8_t selected_wpm = fixed_wpm;
        if (selected_wpm != 0u) {
            dot_ms = 1200.0f / (float)selected_wpm;
            estimated_wpm = selected_wpm;
        }

        float q0 = 0.0f;
        float q1 = 0.0f;
        float q2 = 0.0f;
        float energy = 0.0f;
        for (size_t i = 0u; i < WINDOW_SAMPLES; ++i) {
            float sample = (float)window[i];
            q0 = coefficient * q1 - q2 + sample;
            q2 = q1;
            q1 = q0;
            energy += sample * sample;
        }
        float power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
        if (power < 0.0f) {
            power = 0.0f;
        }
        float amplitude = 2.0f * sqrtf(power) / (float)WINDOW_SAMPLES;
        float rms = sqrtf(energy / (float)WINDOW_SAMPLES);
        float threshold = fmaxf(60.0f, noise_amplitude * (tone ? 2.2f : 3.0f));
        bool detected = amplitude > threshold && amplitude > (0.45f * rms);
        last_signal_amplitude = amplitude;
        last_signal_threshold = threshold;

        if (!tone && amplitude < noise_amplitude * 3.0f) {
            noise_amplitude = 0.995f * noise_amplitude + 0.005f * amplitude;
            if (noise_amplitude < 12.0f) {
                noise_amplitude = 12.0f;
            }
        }

        if (detected != candidate) {
            candidate = detected;
            candidate_count = 1u;
        } else if (candidate_count < 2u) {
            ++candidate_count;
        }

        state_ms += WINDOW_STEP_MS;
        if (candidate_count >= 2u && candidate != tone) {
            uint32_t duration_ms = state_ms;
            state_ms = 0u;
            tone = candidate;

            if (!tone) {
                if (duration_ms >= 20u && duration_ms <= 500u) {
                    if (fixed_wpm == 0u) {
                        dot_ms = update_dot_estimate(
                            timing_history,
                            &timing_history_count,
                            &timing_history_write,
                            (uint16_t)duration_ms,
                            dot_ms
                        );
                    }
                    if (pattern_length + 1u < sizeof(pattern)) {
                        pattern[pattern_length++] =
                            (float)duration_ms < 2.0f * dot_ms ? '.' : '-';
                        pattern[pattern_length] = '\0';
                    }
                    float wpm = fixed_wpm != 0u ?
                        (float)fixed_wpm : 1200.0f / dot_ms;
                    if (wpm < 5.0f) wpm = 5.0f;
                    if (wpm > 99.0f) wpm = 99.0f;
                    estimated_wpm = (uint8_t)(wpm + 0.5f);
                }
                character_sent = false;
                word_sent = false;
            } else {
                stream_active = true;
                stream_end_sent = false;
            }
        }

        /*
         * The two-window tone debounce consumes part of a measured silence.
         * 1.25 DIT remains above an element gap while retaining short
         * letter gaps after that detection latency.
         */
        if (!tone && pattern_length != 0u && !character_sent &&
            (float)state_ms >= 1.25f * dot_ms) {
            queue_event(decode_pattern(pattern));
            pattern_length = 0u;
            pattern[0] = '\0';
            character_sent = true;
        }
        /* Keep the word threshold high enough for Farnsworth letter spacing. */
        if (!tone && character_sent && !word_sent &&
            (float)state_ms >= 5.0f * dot_ms) {
            queue_event(' ');
            word_sent = true;
        }
        if (!tone && stream_active && !stream_end_sent && state_ms >= 2000u) {
            queue_event('\n');
            stream_active = false;
            stream_end_sent = true;
            timing_history_count = 0u;
            timing_history_write = 0u;
            dot_ms = fixed_wpm != 0u ? 1200.0f / (float)fixed_wpm : 60.0f;
            estimated_wpm = fixed_wpm != 0u ? fixed_wpm : 20u;
        }

        memmove(
            window,
            &window[WINDOW_HOP_SAMPLES],
            (WINDOW_SAMPLES - WINDOW_HOP_SAMPLES) * sizeof(window[0])
        );
        window_count = WINDOW_SAMPLES - WINDOW_HOP_SAMPLES;
    }
}

bool cw_decoder_init(void)
{
    sample_write = 0u;
    sample_read = 0u;
    event_write = 0u;
    event_read = 0u;
    dropped_samples = 0u;
    decoder_enabled = true;
    multicore_launch_core1(decoder_core);
    return true;
}

void cw_decoder_submit_audio(const int16_t *samples, size_t frame_count)
{
    if (!decoder_enabled || samples == NULL) {
        return;
    }
    for (size_t i = 0u; i < frame_count; ++i) {
        int32_t boosted = (int32_t)samples[i] * INPUT_GAIN;
        if (boosted > INT16_MAX) {
            boosted = INT16_MAX;
        } else if (boosted < INT16_MIN) {
            boosted = INT16_MIN;
        }
        decimation_sum += boosted;
        if (++decimation_count != DECIMATION) {
            continue;
        }
        int16_t decimated = (int16_t)(decimation_sum / (int32_t)DECIMATION);
        decimation_count = 0u;
        decimation_sum = 0;

        uint32_t write = sample_write;
        uint32_t next = (write + 1u) & SAMPLE_RING_MASK;
        if (next == sample_read) {
            ++dropped_samples;
            continue;
        }
        sample_ring[write] = decimated;
        __dmb();
        sample_write = next;
    }
}

void cw_decoder_task(void)
{
    while (event_read != event_write) {
        uint32_t read = event_read;
        char character = event_ring[read];
        __dmb();
        event_read = (read + 1u) & EVENT_RING_MASK;
        if (character == '\n') {
            control_console_end_cw_line();
        } else {
            control_console_publish_cw_char(character);
        }
        cw_text_display_put_char(character, false);
    }
}

void cw_decoder_set_enabled(bool enabled)
{
    decoder_enabled = enabled;
}

/*
 * Convert a Goertzel amplitude (measured after INPUT_GAIN) back to dBFS of
 * the original, pre-gain signal, so 0 dBFS matches true full scale.
 */
static float amplitude_to_dbfs(float amplitude)
{
    float true_amplitude = amplitude / (float)INPUT_GAIN;
    if (true_amplitude < 1.0f) {
        true_amplitude = 1.0f;
    }
    return 20.0f * log10f(true_amplitude / 32768.0f);
}

float cw_decoder_get_signal_level(void)
{
    return amplitude_to_dbfs(last_signal_amplitude);
}

float cw_decoder_get_threshold(void)
{
    return amplitude_to_dbfs(last_signal_threshold);
}

bool cw_decoder_get_enabled(void)
{
    return decoder_enabled;
}

bool cw_decoder_set_frequency(uint16_t frequency_hz)
{
    if (frequency_hz < 300u || frequency_hz > 1200u) {
        return false;
    }
    requested_frequency_hz = frequency_hz;
    return true;
}

uint16_t cw_decoder_get_frequency(void)
{
    return requested_frequency_hz;
}

uint8_t cw_decoder_get_wpm(void)
{
    return estimated_wpm;
}

bool cw_decoder_set_fixed_wpm(uint8_t wpm)
{
    if (wpm != 0u && (wpm < 5u || wpm > 60u)) {
        return false;
    }
    fixed_wpm = wpm;
    estimated_wpm = wpm != 0u ? wpm : 20u;
    return true;
}

uint8_t cw_decoder_get_fixed_wpm(void)
{
    return fixed_wpm;
}

uint32_t cw_decoder_get_dropped_samples(void)
{
    return dropped_samples;
}
