#include "pn532_srix.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Costanti SRIX
#define SRIX4K_BLOCKS 128
#define SRIX_BLOCK_LENGTH 4
#define SRIX4K_BYTES (SRIX4K_BLOCKS * SRIX_BLOCK_LENGTH) // 512

struct srix_flag {
    uint32_t memory[4];
};

static inline struct srix_flag srix_flag_init() {
    struct srix_flag f;
    f.memory[0] = f.memory[1] = f.memory[2] = f.memory[3] = 0;
    return f;
}
static inline void srix_flag_add(struct srix_flag *f, uint8_t b) {
    if (b < 128) f->memory[b / 32] |= 1u << (b % 32);
}

static inline void srix_flag_remove(struct srix_flag *f, uint8_t b) {
    if (b < 128) f->memory[b / 32] &= ~(1u << (b % 32));
}

static inline bool srix_flag_get(struct srix_flag *f, uint8_t b) {
    return b < 128 && ((f->memory[b / 32] >> (b % 32)) & 1u);
}

static inline bool srix_flag_isModified(struct srix_flag *f) {
    return (f->memory[0] | f->memory[1] | f->memory[2] | f->memory[3]) > 0;
}

struct srix_t {
    uint8_t eeprom[SRIX4K_BLOCKS][SRIX_BLOCK_LENGTH];
    uint64_t uid;
    struct srix_flag srixFlag;
};

struct mykey_t {
    struct srix_t *srix4k;
    uint32_t encryptionKey;
};

bool mikai_read_tag(struct mykey_t *key, Arduino_PN532_SRIX *nfc);
void mikai_get_info_string(struct mykey_t *key, char *out, size_t outLen);
int mikai_add_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year);
int mikai_write_modified_blocks(struct mykey_t *key, Arduino_PN532_SRIX *nfc);
bool mikai_has_pending_writes(struct mykey_t *key);
int mikai_export_vendor(struct mykey_t *key, uint8_t buffer[8]);
uint16_t mikai_get_current_credit(struct mykey_t *key);
void mikai_reset_key(struct mykey_t *key);
void mikai_import_vendor(struct mykey_t *key, const uint8_t block18[4], const uint8_t block19[4]);
int mikai_set_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year);
bool mikai_is_reset(struct mykey_t *key);

// Non usate
void mikai_reset_otp(struct mykey_t *key);
void mikai_export_dump(struct mykey_t *key, uint64_t *uid_out, uint8_t eeprom_out[SRIX4K_BYTES]);
void mikai_modify_block(struct mykey_t *key, const uint8_t block[4], uint8_t blockNum);
bool mikai_check_lock_id(struct mykey_t *key);
