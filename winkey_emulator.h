#ifndef WINKEY_EMULATOR_H
#define WINKEY_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#define WINKEY_PERSISTENT_EEPROM_SIZE 256u

typedef enum {
    WINKEY_MODE_IAMBIC_B = 0,
    WINKEY_MODE_IAMBIC_A,
    WINKEY_MODE_ULTIMATIC,
    WINKEY_MODE_BUG
} winkey_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

void winkey_emulator_init(void);
void winkey_emulator_task(void);
void winkey_emulator_set_speed(uint8_t wpm);
uint8_t winkey_emulator_get_speed(void);
void winkey_emulator_set_sidetone_frequency(uint16_t frequency_hz);
uint16_t winkey_emulator_get_sidetone_frequency(void);
void winkey_emulator_set_mode(winkey_mode_t mode);
winkey_mode_t winkey_emulator_get_mode(void);
const char *winkey_emulator_mode_name(winkey_mode_t mode);
void winkey_emulator_set_paddle_swap(bool enabled);
bool winkey_emulator_get_paddle_swap(void);
void winkey_emulator_set_weight(uint8_t percent);
uint8_t winkey_emulator_get_weight(void);
void winkey_emulator_export_eeprom(
    uint8_t output[WINKEY_PERSISTENT_EEPROM_SIZE]
);
bool winkey_emulator_import_eeprom(
    const uint8_t input[WINKEY_PERSISTENT_EEPROM_SIZE]
);

#ifdef __cplusplus
}
#endif

#endif
