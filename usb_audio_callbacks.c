#include "tusb.h"
#include "audio_i2s_test.h"
#include "usb_audio_callbacks.h"
#include "usb_os_guessing.h"

static volatile usb_audio_stats_t audio_stats;

static bool mute[3];
static int16_t volume[3];
static audio_control_range_2_n_t(1) volume_range[3];
static bool mute_spk[3];
static int16_t volume_spk[3];
static audio_control_range_2_n_t(1) volume_range_spk[3];
static uint32_t sample_rate = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint32_t sample_rate_spk = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;

static void audio_controls_init(void);

/* Keep the real I2S queue inside this window so USB feedback follows it. */
#define USB_AUDIO_QUEUE_LOW_FRAMES   192u
#define USB_AUDIO_QUEUE_HIGH_FRAMES  384u
#define USB_AUDIO_GAIN_Q15_MAX       32767

static int32_t volume_db_to_gain_q15(int32_t volume_db_8_8)
{
    if (volume_db_8_8 >= 0) {
        return USB_AUDIO_GAIN_Q15_MAX;
    }

    int32_t attenuation = -volume_db_8_8;
    int32_t steps = (attenuation + 128) / 256;
    if (steps > 50) {
        steps = 50;
    }

    /* 10^(-1/20) in Q15: one multiplication for each whole dB. */
    int32_t gain = USB_AUDIO_GAIN_Q15_MAX;
    while (steps-- > 0) {
        gain = (gain * 29205 + 16384) >> 15;
    }
    return gain;
}

static int32_t speaker_channel_gain_q15(uint8_t channel)
{
    if (mute_spk[0] || mute_spk[channel]) {
        return 0;
    }

    /* UAC2 master and per-channel attenuations are cumulative. */
    int32_t volume_db_8_8 =
        (int32_t)volume_spk[0] + (int32_t)volume_spk[channel];
    if (volume_db_8_8 < (-50 * 256)) {
        volume_db_8_8 = -50 * 256;
    }
    return volume_db_to_gain_q15(volume_db_8_8);
}

void usb_audio_get_stats(usb_audio_stats_t *stats)
{
    if (stats != NULL) {
        *stats = audio_stats;
    }
}

void usb_audio_task(void)
{
#if CFG_TUD_AUDIO > 0 && CFG_TUD_AUDIO_ENABLE_EP_OUT
    audio_controls_init();
#ifdef RP2350_USB_AUDIO_CDC_TEST
    const uint8_t func_id = 1;
#else
    const uint8_t func_id = 0;
#endif
#if defined(RP2350_USB_AUDIO_OUT_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
    int16_t pcm[96];
    int16_t mono[48];
#else
    int16_t pcm[48];
#endif

    for (unsigned reads = 0; reads < 4; ++reads) {
        uint32_t queued_frames = audio_i2s_playback_queued_frames();
        uint16_t available = tud_audio_n_available(func_id);
#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
        if (queued_frames >= USB_AUDIO_QUEUE_HIGH_FRAMES &&
            available < (6u * CFG_TUD_AUDIO_EP_SZ_OUT)) {
            /*
             * Leaving data in TinyUSB's FIFO raises its feedback fill level,
             * asking the host to slow down instead of overflowing I2S PCM.
             * At six packets the emergency path below drains one packet so
             * there is still room while the host reacts.
             */
            break;
        }
        uint16_t minimum = queued_frames < USB_AUDIO_QUEUE_LOW_FRAMES
            ? (uint16_t)(2u * sizeof(int16_t))
            : (uint16_t)(2u * CFG_TUD_AUDIO_EP_SZ_OUT);
        if (available < minimum) {
#else
        if (available < sizeof(int16_t)) {
#endif
            break;
        }

        uint16_t bytes = available > sizeof(pcm) ? sizeof(pcm) : available;
#if defined(RP2350_USB_AUDIO_OUT_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
        bytes &= (uint16_t)~3u;
#else
        bytes &= (uint16_t)~1u;
#endif
        uint16_t read = tud_audio_n_read(func_id, pcm, bytes);
        if (read == 0) {
            break;
        }

#if defined(RP2350_USB_AUDIO_OUT_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
        size_t frames = read / (2u * sizeof(int16_t));
        int32_t left_gain = speaker_channel_gain_q15(1u);
        int32_t right_gain = speaker_channel_gain_q15(2u);
        for (size_t i = 0; i < frames; ++i) {
            int32_t left =
                ((int32_t)pcm[2u * i] * left_gain) /
                USB_AUDIO_GAIN_Q15_MAX;
            int32_t right =
                ((int32_t)pcm[2u * i + 1u] * right_gain) /
                USB_AUDIO_GAIN_Q15_MAX;
            int32_t mixed = left + right;
            mono[i] = (int16_t)(mixed / 2);
        }
        size_t accepted = audio_i2s_write_mono16(mono, frames);
#else
        size_t frames = read / sizeof(int16_t);
        size_t accepted = audio_i2s_write_mono16(pcm, frames);
#endif

        audio_stats.packets++;
        audio_stats.bytes += read;
        audio_stats.accepted_frames += (uint32_t)accepted;
        audio_stats.dropped_frames += (uint32_t)(frames - accepted);
        audio_stats.last_func_id = func_id;

        for (size_t i = 0; i < frames; ++i) {
#if defined(RP2350_USB_AUDIO_OUT_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
            int32_t value = mono[i];
#else
            int32_t value = pcm[i];
#endif
            uint16_t magnitude = (uint16_t)(value < 0 ? -value : value);
            if (magnitude > audio_stats.peak) {
                audio_stats.peak = magnitude;
            }
        }
    }
#endif
}

#if CFG_TUD_AUDIO > 0

// For microphone-only: TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR uses standard entity IDs
// We define minimal state and callbacks for enumeration to work

#define UAC2_ENTITY_INPUT_TERM_MIC      0x01
#define UAC2_ENTITY_FEATURE_UNIT_MIC    0x02
#define UAC2_ENTITY_OUTPUT_TERM_USB_MIC 0x03
#define UAC2_ENTITY_CLOCK_SOURCE        0x04

#ifdef RP2350_USB_AUDIO_CDC_TEST
#define ITF_AUDIO_CONTROL_MIC           0x00
#define ITF_AUDIO_CONTROL_SPK           0x02
#elif defined(RP2350_USB_SPK_CDC_TEST) || defined(RP2350_USB_AUDIO_OUT_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
#define ITF_AUDIO_CONTROL_SPK           0x00
#define ITF_AUDIO_CONTROL_MIC           0x02
#else
#define ITF_AUDIO_CONTROL_MIC           0x00
#define ITF_AUDIO_CONTROL_SPK           0x02
#endif

static void audio_controls_init(void)
{
    static bool initialized;
    if (initialized) {
        return;
    }

    initialized = true;
    mute[0] = false;
    mute[1] = false;
    volume[0] = 0;
    volume[1] = 0;
    volume_range[0].wNumSubRanges = 1;
    volume_range[0].subrange[0].bMin = -50 * 256;
    volume_range[0].subrange[0].bMax = 0;
    volume_range[0].subrange[0].bRes = 256;
    volume_range[1] = volume_range[0];
    volume_range[2] = volume_range[0];

    mute_spk[0] = false;
    mute_spk[1] = false;
    volume_spk[0] = 0;
    volume_spk[1] = 0;
    volume_range_spk[0].wNumSubRanges = 1;
    volume_range_spk[0].subrange[0].bMin = -50 * 256;
    volume_range_spk[0].subrange[0].bMax = 0;
    volume_range_spk[0].subrange[0].bRes = 256;
    volume_range_spk[1] = volume_range_spk[0];
    volume_range_spk[2] = volume_range_spk[0];
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;
    (void)p_request;
    audio_controls_init();
    return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;
    (void)p_request;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    audio_controls_init();

    uint8_t channel_num = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    uint8_t itf_num = TU_U16_LOW(p_request->wIndex);

    if (entity_id == UAC2_ENTITY_FEATURE_UNIT_MIC && p_request->bRequest == AUDIO_CS_REQ_CUR) {
        if (channel_num > 2) return false;

        if (ctrl_sel == AUDIO_FU_CTRL_MUTE && p_request->wLength == sizeof(audio_control_cur_1_t)) {
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                mute[channel_num] = ((audio_control_cur_1_t*) pBuff)->bCur;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                mute_spk[channel_num] = ((audio_control_cur_1_t*) pBuff)->bCur;
            } else {
                return false;
            }
            return true;
        }

        if (ctrl_sel == AUDIO_FU_CTRL_VOLUME && p_request->wLength == sizeof(audio_control_cur_2_t)) {
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                volume[channel_num] = ((audio_control_cur_2_t*) pBuff)->bCur;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                volume_spk[channel_num] = ((audio_control_cur_2_t*) pBuff)->bCur;
            } else {
                return false;
            }
            return true;
        }
    }

    if (entity_id == UAC2_ENTITY_CLOCK_SOURCE && p_request->bRequest == AUDIO_CS_REQ_CUR &&
        ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ && p_request->wLength == sizeof(audio_control_cur_4_t)) {
        if (itf_num == ITF_AUDIO_CONTROL_MIC) {
            sample_rate = ((audio_control_cur_4_t*) pBuff)->bCur;
        } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
            sample_rate_spk = ((audio_control_cur_4_t*) pBuff)->bCur;
        } else {
            return false;
        }
        return true;
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    audio_controls_init();
    uint8_t channel_num = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    uint8_t itf_num = TU_U16_LOW(p_request->wIndex);

    if (entity_id == UAC2_ENTITY_FEATURE_UNIT_MIC) {
        if (channel_num > 2) return false;

        if (ctrl_sel == AUDIO_FU_CTRL_MUTE && p_request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t ret;
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                ret.bCur = mute[channel_num] ? 1 : 0;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                ret.bCur = mute_spk[channel_num] ? 1 : 0;
            } else {
                return false;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
        }

        if (ctrl_sel == AUDIO_FU_CTRL_VOLUME) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_2_t ret;
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    ret.bCur = volume[channel_num];
                } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    ret.bCur = volume_spk[channel_num];
                } else {
                    return false;
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &volume_range[channel_num], sizeof(audio_control_range_2_n_t(1)));
                }
                if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &volume_range_spk[channel_num], sizeof(audio_control_range_2_n_t(1)));
                }
                return false;
            }
        }
    }

    if (entity_id == UAC2_ENTITY_CLOCK_SOURCE) {
        if (ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_4_t ret;
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    ret.bCur = sample_rate;
                } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    ret.bCur = sample_rate_spk;
                } else {
                    return false;
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_4_n_t(1) ret;
                uint32_t sr = (itf_num == ITF_AUDIO_CONTROL_SPK) ? CFG_TUD_AUDIO_FUNC_2_SAMPLE_RATE : CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
                ret.wNumSubRanges = 1;
                ret.subrange[0].bMin = sr;
                ret.subrange[0].bMax = sr;
                ret.subrange[0].bRes = 0;
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
        }

        if (ctrl_sel == AUDIO_CS_CTRL_CLK_VALID && p_request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t ret = { .bCur = 1 };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
        }
    }

    return false;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)n_bytes_received;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;

    /*
     * RP2350 TinyUSB receives into a linear endpoint buffer first. The PCM
     * bytes are therefore not available through tud_audio_n_read() until the
     * post-read callback.
     */
    return true;
}

bool tud_audio_rx_done_post_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)n_bytes_received;
    (void)ep_out;
    (void)cur_alt_setting;

    return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)ep_in;
    (void)cur_alt_setting;
#if defined(RP2350_USB_AUDIO_DUPLEX_ONLY) || defined(RP2350_USB_AUDIO_DUPLEX_CDC) || defined(RP2350_USB_AUDIO_DUPLEX_CDC_MIDI)
    if (func_id == 1) {
        int16_t microphone[48] = {0};
        (void)audio_i2s_read_mono16(
            microphone,
            sizeof(microphone) / sizeof(microphone[0])
        );
        (void)tud_audio_n_write(func_id, microphone, sizeof(microphone));
    }
#else
    (void)func_id;
#endif
    return true;
}

#if CFG_TUD_AUDIO_ENABLE_EP_OUT && CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
void tud_audio_feedback_params_cb(
    uint8_t func_id,
    uint8_t alt_itf,
    audio_feedback_params_t *feedback_param)
{
    (void)func_id;
    (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
}

bool tud_audio_feedback_format_correction_cb(uint8_t func_id)
{
    (void)func_id;
    return tud_speed_get() == TUSB_SPEED_FULL &&
           usb_os_guessing_get() == USB_HOST_OS_MACOS;
}
#endif

#endif
