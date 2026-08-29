#include "settings_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_i2s_test.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "winkey_emulator.h"

#define SETTINGS_MAGIC             0x52424b59u /* "RBKY" */
#define SETTINGS_FORMAT_VERSION    3u
#define SETTINGS_PREVIOUS_VERSION  2u
#define SETTINGS_LEGACY_VERSION    1u
#define SETTINGS_RECORD_BYTES      (2u * FLASH_PAGE_SIZE)
#define SETTINGS_STORAGE_BYTES     (2u * FLASH_SECTOR_SIZE)
#define SETTINGS_SLOT0_OFFSET      \
    (PICO_FLASH_SIZE_BYTES - SETTINGS_STORAGE_BYTES)
#define SETTINGS_SLOT1_OFFSET      \
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MUTE_LEAD_MS      10u
#define SETTINGS_FLASH_TIMEOUT_MS  1000u

typedef struct {
    uint8_t master_volume_percent;
    uint8_t sidetone_volume_percent;
    uint16_t sidetone_frequency_hz;
    uint8_t output_mode;
    uint8_t midi_ptt_mode;
    uint8_t winkey_eeprom[WINKEY_PERSISTENT_EEPROM_SIZE];
} settings_payload_t;

typedef struct {
    uint8_t master_volume_percent;
    uint8_t sidetone_volume_percent;
    uint16_t sidetone_frequency_hz;
    uint8_t winkey_eeprom[WINKEY_PERSISTENT_EEPROM_SIZE];
} settings_legacy_payload_t;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t payload_size;
    uint32_t sequence;
    uint32_t crc32;
    settings_payload_t payload;
    uint8_t reserved[
        SETTINGS_RECORD_BYTES - 16u - sizeof(settings_payload_t)
    ];
} settings_record_t;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t payload_size;
    uint32_t sequence;
    uint32_t crc32;
    settings_legacy_payload_t payload;
    uint8_t reserved[
        SETTINGS_RECORD_BYTES - 16u - sizeof(settings_legacy_payload_t)
    ];
} settings_legacy_record_t;

_Static_assert(sizeof(settings_payload_t) == 262u,
               "Unexpected persistent settings payload size");
_Static_assert(sizeof(settings_legacy_payload_t) == 260u,
               "Unexpected legacy settings payload size");
_Static_assert(sizeof(settings_record_t) == SETTINGS_RECORD_BYTES,
               "Persistent settings record must occupy two flash pages");
_Static_assert(sizeof(settings_legacy_record_t) == SETTINGS_RECORD_BYTES,
               "Legacy settings record must occupy two flash pages");

typedef enum {
    STORAGE_IDLE = 0,
    STORAGE_WAIT_FOR_AUDIO_MUTE
} storage_state_t;

typedef struct {
    uint32_t flash_offset;
    const uint8_t *record_data;
} flash_write_context_t;

extern uint8_t __flash_binary_end;

static wm8960_t *storage_codec;
static bool storage_available;
static int active_slot = -1;
static uint32_t active_sequence;
static settings_payload_t saved_payload;
static settings_record_t pending_record __attribute__((aligned(4)));
static flash_write_context_t flash_write_context;
static storage_state_t storage_state;
static uint32_t storage_request_ms;
static settings_save_result_t save_result;

static uint32_t record_crc32(const void *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    const size_t crc_offset = offsetof(settings_record_t, crc32);
    uint32_t crc = 0xffffffffu;

    for (size_t index = 0u; index < SETTINGS_RECORD_BYTES; ++index) {
        uint8_t value =
            index >= crc_offset && index < crc_offset + sizeof(uint32_t)
                ? 0u
                : bytes[index];
        crc ^= value;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static bool payload_is_sane(const settings_payload_t *payload)
{
    return payload->master_volume_percent <= 100u &&
           payload->sidetone_volume_percent <= 100u &&
           payload->sidetone_frequency_hz >= 300u &&
           payload->sidetone_frequency_hz <= 1200u &&
           payload->output_mode >= WM8960_OUTPUT_HEADPHONES &&
           payload->output_mode <= WM8960_OUTPUT_BOTH &&
           payload->midi_ptt_mode <= WINKEY_MIDI_PTT_THETIS &&
           payload->winkey_eeprom[0] == 0xa5u;
}

static bool legacy_payload_is_sane(
    const settings_legacy_payload_t *payload)
{
    return payload->master_volume_percent <= 100u &&
           payload->sidetone_volume_percent <= 100u &&
           payload->sidetone_frequency_hz >= 300u &&
           payload->sidetone_frequency_hz <= 1200u &&
           payload->winkey_eeprom[0] == 0xa5u;
}

static bool previous_payload_is_sane(const settings_payload_t *payload)
{
    return payload->master_volume_percent <= 100u &&
           payload->sidetone_volume_percent <= 100u &&
           payload->sidetone_frequency_hz >= 300u &&
           payload->sidetone_frequency_hz <= 1200u &&
           payload->output_mode >= WM8960_OUTPUT_HEADPHONES &&
           payload->output_mode <= WM8960_OUTPUT_BOTH &&
           payload->winkey_eeprom[0] == 0xa5u;
}

static bool record_is_valid(const settings_record_t *record)
{
    return record->magic == SETTINGS_MAGIC &&
           record->format_version == SETTINGS_FORMAT_VERSION &&
           record->payload_size == sizeof(settings_payload_t) &&
           payload_is_sane(&record->payload) &&
           record->crc32 == record_crc32(record);
}

static bool legacy_record_is_valid(const settings_record_t *record)
{
    const settings_legacy_record_t *legacy =
        (const settings_legacy_record_t *)record;
    return legacy->magic == SETTINGS_MAGIC &&
           legacy->format_version == SETTINGS_LEGACY_VERSION &&
           legacy->payload_size == sizeof(settings_legacy_payload_t) &&
           legacy_payload_is_sane(&legacy->payload) &&
           legacy->crc32 == record_crc32(legacy);
}

static bool previous_record_is_valid(const settings_record_t *record)
{
    return record->magic == SETTINGS_MAGIC &&
           record->format_version == SETTINGS_PREVIOUS_VERSION &&
           record->payload_size == sizeof(settings_payload_t) &&
           previous_payload_is_sane(&record->payload) &&
           record->crc32 == record_crc32(record);
}

static bool stored_record_is_valid(const settings_record_t *record)
{
    return record_is_valid(record) || previous_record_is_valid(record) ||
           legacy_record_is_valid(record);
}

static bool sequence_is_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

static const settings_record_t *flash_record(unsigned slot)
{
    uint32_t offset = slot == 0u
        ? SETTINGS_SLOT0_OFFSET
        : SETTINGS_SLOT1_OFFSET;
    return (const settings_record_t *)(XIP_BASE + offset);
}

static void capture_payload(settings_payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->master_volume_percent =
        storage_codec->headphone_volume_percent;
    payload->sidetone_volume_percent =
        audio_i2s_get_sidetone_volume();
    payload->sidetone_frequency_hz =
        winkey_emulator_get_sidetone_frequency();
    payload->output_mode = (uint8_t)storage_codec->output;
    payload->midi_ptt_mode = (uint8_t)winkey_emulator_get_midi_ptt_mode();
    winkey_emulator_export_eeprom(payload->winkey_eeprom);
}

static void convert_legacy_payload(
    settings_payload_t *payload,
    const settings_legacy_payload_t *legacy)
{
    memset(payload, 0, sizeof(*payload));
    payload->master_volume_percent = legacy->master_volume_percent;
    payload->sidetone_volume_percent = legacy->sidetone_volume_percent;
    payload->sidetone_frequency_hz = legacy->sidetone_frequency_hz;
    payload->output_mode = WM8960_OUTPUT_BOTH;
    payload->midi_ptt_mode = WINKEY_MIDI_PTT_PIHPSDR;
    memcpy(
        payload->winkey_eeprom,
        legacy->winkey_eeprom,
        sizeof(payload->winkey_eeprom)
    );
}

static bool apply_payload(const settings_payload_t *payload)
{
    if (!payload_is_sane(payload) ||
        !winkey_emulator_import_eeprom(payload->winkey_eeprom)) {
        return false;
    }

    if (!wm8960_set_headphone_volume(
            storage_codec,
            payload->master_volume_percent)) {
        return false;
    }
    if (!wm8960_set_speaker_volume(
            storage_codec,
            payload->master_volume_percent)) {
        return false;
    }
    if (!wm8960_set_output(
            storage_codec,
            (wm8960_output_t)payload->output_mode)) {
        return false;
    }
    audio_i2s_set_sidetone_volume(payload->sidetone_volume_percent);
    winkey_emulator_set_midi_ptt_mode(
        (winkey_midi_ptt_mode_t)payload->midi_ptt_mode
    );
    winkey_emulator_set_sidetone_frequency(
        payload->sidetone_frequency_hz
    );
    return true;
}

static void __not_in_flash_func(write_settings_record)(void *parameter)
{
    flash_write_context_t *context = (flash_write_context_t *)parameter;
    flash_range_erase(context->flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(
        context->flash_offset,
        context->record_data,
        sizeof(settings_record_t)
    );
}

bool settings_storage_init(wm8960_t *codec)
{
    if (codec == NULL) {
        return false;
    }
    storage_codec = codec;

    uintptr_t binary_end_offset =
        (uintptr_t)&__flash_binary_end - (uintptr_t)XIP_BASE;
    if (binary_end_offset > SETTINGS_SLOT0_OFFSET) {
        storage_available = false;
        return false;
    }

    const settings_record_t *slot0 = flash_record(0u);
    const settings_record_t *slot1 = flash_record(1u);
    bool slot0_valid = stored_record_is_valid(slot0);
    bool slot1_valid = stored_record_is_valid(slot1);

    const settings_record_t *selected = NULL;
    if (slot0_valid && slot1_valid) {
        if (sequence_is_newer(slot1->sequence, slot0->sequence)) {
            active_slot = 1;
            selected = slot1;
        } else {
            active_slot = 0;
            selected = slot0;
        }
    } else if (slot0_valid) {
        active_slot = 0;
        selected = slot0;
    } else if (slot1_valid) {
        active_slot = 1;
        selected = slot1;
    }

    if (selected != NULL) {
        settings_payload_t restored;
        if (selected->format_version == SETTINGS_FORMAT_VERSION) {
            memcpy(&restored, &selected->payload, sizeof(restored));
        } else if (selected->format_version == SETTINGS_PREVIOUS_VERSION) {
            memcpy(&restored, &selected->payload, sizeof(restored));
            restored.midi_ptt_mode = WINKEY_MIDI_PTT_PIHPSDR;
        } else {
            const settings_legacy_record_t *legacy =
                (const settings_legacy_record_t *)selected;
            convert_legacy_payload(&restored, &legacy->payload);
        }
        if (!apply_payload(&restored)) {
            storage_available = false;
            return false;
        }
        active_sequence = selected->sequence;
        memcpy(&saved_payload, &restored, sizeof(saved_payload));
    } else {
        active_slot = -1;
        active_sequence = 0u;
        capture_payload(&saved_payload);
    }

    storage_state = STORAGE_IDLE;
    save_result = SETTINGS_SAVE_RESULT_NONE;
    storage_available = true;
    return true;
}

bool settings_storage_reload(void)
{
    return storage_available && storage_codec != NULL &&
           storage_state == STORAGE_IDLE &&
           apply_payload(&saved_payload);
}

settings_save_request_t settings_storage_request_save(void)
{
    if (!storage_available || storage_codec == NULL) {
        return SETTINGS_SAVE_UNAVAILABLE;
    }
    if (storage_state != STORAGE_IDLE) {
        return SETTINGS_SAVE_BUSY;
    }

    settings_payload_t current;
    capture_payload(&current);
    if (memcmp(&current, &saved_payload, sizeof(current)) == 0) {
        return SETTINGS_SAVE_UNCHANGED;
    }

    memset(&pending_record, 0, sizeof(pending_record));
    pending_record.magic = SETTINGS_MAGIC;
    pending_record.format_version = SETTINGS_FORMAT_VERSION;
    pending_record.payload_size = sizeof(settings_payload_t);
    pending_record.sequence = active_sequence + 1u;
    memcpy(&pending_record.payload, &current, sizeof(current));
    pending_record.crc32 = record_crc32(&pending_record);

    int target_slot = active_slot == 0 ? 1 : 0;
    flash_write_context.flash_offset = target_slot == 0
        ? SETTINGS_SLOT0_OFFSET
        : SETTINGS_SLOT1_OFFSET;
    flash_write_context.record_data = (const uint8_t *)&pending_record;

    save_result = SETTINGS_SAVE_RESULT_NONE;
    storage_request_ms = to_ms_since_boot(get_absolute_time());
    storage_state = STORAGE_WAIT_FOR_AUDIO_MUTE;
    audio_i2s_set_usb_playback_mute_source(
        AUDIO_USB_MUTE_SOURCE_STORAGE,
        true
    );
    return SETTINGS_SAVE_ACCEPTED;
}

void settings_storage_task(void)
{
    if (storage_state != STORAGE_WAIT_FOR_AUDIO_MUTE) {
        return;
    }

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - storage_request_ms) < SETTINGS_MUTE_LEAD_MS) {
        return;
    }

    int result = flash_safe_execute(
        write_settings_record,
        &flash_write_context,
        SETTINGS_FLASH_TIMEOUT_MS
    );

    int target_slot = active_slot == 0 ? 1 : 0;
    const settings_record_t *written = flash_record((unsigned)target_slot);
    bool verified = result == PICO_OK && record_is_valid(written) &&
                    memcmp(written, &pending_record,
                           sizeof(pending_record)) == 0;

    if (verified) {
        active_slot = target_slot;
        active_sequence = pending_record.sequence;
        memcpy(
            &saved_payload,
            &pending_record.payload,
            sizeof(saved_payload)
        );
        save_result = SETTINGS_SAVE_RESULT_OK;
    } else {
        save_result = SETTINGS_SAVE_RESULT_ERROR;
    }

    storage_state = STORAGE_IDLE;
    audio_i2s_set_usb_playback_mute_source(
        AUDIO_USB_MUTE_SOURCE_STORAGE,
        false
    );
}

settings_save_result_t settings_storage_take_result(void)
{
    settings_save_result_t result = save_result;
    save_result = SETTINGS_SAVE_RESULT_NONE;
    return result;
}

bool settings_storage_is_busy(void)
{
    return storage_state != STORAGE_IDLE;
}
