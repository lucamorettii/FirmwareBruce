#include "MikaiLogic.h"
#include <Arduino.h>

// NFC brifge to Arduino_PN532_SRIX
static bool nfc_wait_and_select(Arduino_PN532_SRIX *nfc) {
    // First call SRIX_init() to put PN532 into the right mode for ISO14443B2SR
    if (!nfc->SRIX_init()) return false;

    // Poll until a tag responds to INITIATE+SELECT
    for (int attempt = 0; attempt < 60; attempt++) { // up to ~6 seconds
        if (nfc->SRIX_initiate_select()) return true;
        delay(100);
    }
    return false;
}

static void nfc_reselect(Arduino_PN532_SRIX *nfc) {
    nfc->SRIX_init();
    for (int i = 0; i < 60; i++) {
        if (nfc->SRIX_initiate_select()) return;
        delay(100);
    }
}

static void read_block(Arduino_PN532_SRIX *nfc, uint8_t rx[4], uint8_t blockNum) {
    while (!nfc->SRIX_read_block(blockNum, rx)) {
        Serial.printf("[MIKAI] Block %02X read failed - reposition tag\n", blockNum);
        nfc_reselect(nfc);
    }
}

static void write_block(Arduino_PN532_SRIX *nfc, struct srix_t *target, uint8_t blockNum) {
    Serial.printf("[MIKAI] Writing block %02X...\n", blockNum);
    while (true) {
        nfc->SRIX_write_block(blockNum, target->eeprom[blockNum]);

        uint8_t check[4];
        read_block(nfc, check, blockNum);

        if (memcmp(target->eeprom[blockNum], check, 4) == 0) return;

        Serial.printf("[MIKAI] Block %02X verify failed - retrying\n", blockNum);
        nfc_reselect(nfc);
    }
}

// Private
static void encode_decode_block(uint8_t block[4]) {
    uint8_t in[4] = {block[0], block[1], block[2], block[3]};
    block[0] = (in[0] & 0xC0);
    block[0] |= ((in[1] & 0xC0) >> 2);
    block[0] |= ((in[2] & 0xC0) >> 4);
    block[0] |= ((in[3] & 0xC0) >> 6);
    block[1] = ((in[0] & 0x30) << 2);
    block[1] |= (in[1] & 0x30);
    block[1] |= ((in[2] & 0x30) >> 2);
    block[1] |= ((in[3] & 0x30) >> 4);
    block[2] = ((in[0] & 0x0C) << 4);
    block[2] |= ((in[1] & 0x0C) << 2);
    block[2] |= (in[2] & 0x0C);
    block[2] |= ((in[3] & 0x0C) >> 2);
    block[3] = ((in[0] & 0x03) << 6);
    block[3] |= ((in[1] & 0x03) << 4);
    block[3] |= ((in[2] & 0x03) << 2);
    block[3] |= (in[3] & 0x03);
}

static void calculateBlockChecksum(uint8_t block[4], uint8_t blockNum) {
    block[0] = 0xFF - blockNum - (block[3] & 0x0F) - ((block[3] >> 4) & 0x0F) - (block[2] & 0x0F) -
               ((block[2] >> 4) & 0x0F) - (block[1] & 0x0F) - ((block[1] >> 4) & 0x0F);
}

static uint32_t days_difference(int day, int month, int year) {
    if (month < 3) {
        year--;
        month += 12;
    }
    return (uint32_t)(year * 365 + year / 4 - year / 100 + year / 400 + (month * 153 + 3) / 5 + day - 728692);
}

static void calculateEncryptionKey(struct mykey_t *key) {
    uint32_t otp = (((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][3]) << 24) |
                    ((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][2]) << 16) |
                    ((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][1]) << 8) |
                    (uint32_t)(0xFFu - key->srix4k->eeprom[0x06][0])) +
                   1u;

    uint8_t b18[4], b19[4];
    memcpy(b18, key->srix4k->eeprom[0x18], 4);
    memcpy(b19, key->srix4k->eeprom[0x19], 4);
    encode_decode_block(b18);
    encode_decode_block(b19);

    uint32_t masterKey =
        (uint32_t)(key->srix4k->uid * (uint64_t)((((uint32_t)b18[2] << 24) | ((uint32_t)b18[3] << 16) |
                                                  ((uint32_t)b19[2] << 8) | (uint32_t)b19[3]) +
                                                 1u));

    key->encryptionKey = masterKey * otp;
}

static uint8_t get_current_transaction_offset(struct mykey_t *key) {
    if (key->srix4k->eeprom[0x3C][1] == 0xFF) return 0x07;
    uint8_t cur[4];
    cur[0] = key->srix4k->eeprom[0x3C][0];
    cur[1] = key->srix4k->eeprom[0x3C][1] ^ key->srix4k->eeprom[0x07][1];
    cur[2] = key->srix4k->eeprom[0x3C][2] ^ key->srix4k->eeprom[0x07][2];
    cur[3] = key->srix4k->eeprom[0x3C][3] ^ key->srix4k->eeprom[0x07][3];
    encode_decode_block(cur);
    if (cur[1] > 7) {
        Serial.println("[MIKAI] Bad tx pointer, using 7.");
        return 0x07;
    }
    return cur[1];
}

// Pubbliche
bool mikai_read_tag(struct mykey_t *key, Arduino_PN532_SRIX *nfc) {
    memset(key->srix4k, 0, sizeof(struct srix_t));
    key->srix4k->srixFlag = srix_flag_init();
    key->encryptionKey = 0;

    Serial.println("[MIKAI] Waiting for SRIX4K tag...");
    if (!nfc_wait_and_select(nfc)) {
        Serial.println("[MIKAI] No tag found.");
        return false;
    }

    // Read UID
    uint8_t uid_bytes[8];
    if (!nfc->SRIX_get_uid(uid_bytes)) {
        Serial.println("[MIKAI] UID read failed.");
        return false;
    }
    if (uid_bytes[7] != 0xD0 || uid_bytes[6] != 0x02) {
        Serial.println("[MIKAI] Not an SRIX4K tag (bad UID header).");
        return false;
    }
    key->srix4k->uid = ((uint64_t)uid_bytes[7] << 56) | ((uint64_t)uid_bytes[6] << 48) |
                       ((uint64_t)uid_bytes[5] << 40) | ((uint64_t)uid_bytes[4] << 32) |
                       ((uint64_t)uid_bytes[3] << 24) | ((uint64_t)uid_bytes[2] << 16) |
                       ((uint64_t)uid_bytes[1] << 8) | (uint64_t)uid_bytes[0];
    Serial.printf("[MIKAI] UID: %016llX\n", key->srix4k->uid);

    // Read all 128 blocks
    Serial.printf("[MIKAI] Reading %u blocks...\n", SRIX4K_BLOCKS);
    for (uint8_t i = 0; i < SRIX4K_BLOCKS; i++) read_block(nfc, key->srix4k->eeprom[i], i);

    calculateEncryptionKey(key);
    Serial.printf("[MIKAI] SK: %08lX\n", key->encryptionKey);
    return true;
}

bool mikai_is_reset(struct mykey_t *key) {
    return key->srix4k->eeprom[0x18][0] == 0x8F && key->srix4k->eeprom[0x18][1] == 0xCD &&
           key->srix4k->eeprom[0x18][2] == 0x0F && key->srix4k->eeprom[0x18][3] == 0x48 &&
           key->srix4k->eeprom[0x19][0] == 0xC0 && key->srix4k->eeprom[0x19][1] == 0x82 &&
           key->srix4k->eeprom[0x19][2] == 0x00 && key->srix4k->eeprom[0x19][3] == 0x07;
}

bool mikai_check_lock_id(struct mykey_t *key) {
    uint8_t cc[4];
    cc[0] = key->srix4k->eeprom[0x21][0] ^ (key->encryptionKey >> 24);
    cc[1] = key->srix4k->eeprom[0x21][1] ^ (key->encryptionKey >> 16);
    cc[2] = key->srix4k->eeprom[0x21][2] ^ (key->encryptionKey >> 8);
    cc[3] = key->srix4k->eeprom[0x21][3] ^ key->encryptionKey;
    encode_decode_block(cc);
    uint8_t saved = cc[0];
    calculateBlockChecksum(cc, 0x21);
    return (key->srix4k->eeprom[0x05][3] == 0x7F && saved != cc[0]);
}

uint16_t mikai_get_current_credit(struct mykey_t *key) {
    uint8_t cc[4];
    cc[0] = key->srix4k->eeprom[0x21][0] ^ (key->encryptionKey >> 24);
    cc[1] = key->srix4k->eeprom[0x21][1] ^ (key->encryptionKey >> 16);
    cc[2] = key->srix4k->eeprom[0x21][2] ^ (key->encryptionKey >> 8);
    cc[3] = key->srix4k->eeprom[0x21][3] ^ key->encryptionKey;
    encode_decode_block(cc);
    return (uint16_t)((cc[2] << 8) | cc[3]);
}

void mikai_get_info_string(struct mykey_t *key, char *out, size_t outLen) {
    char tmp[80];
    out[0] = '\0';

    bool lockId = mikai_check_lock_id(key);
    snprintf(tmp, sizeof(tmp), "Lock ID: %s\n", lockId ? "YES" : "no");
    strncat(out, tmp, outLen - strlen(out) - 1);

    if (!lockId) {
        if (!mikai_is_reset(key)) {
            uint16_t c = mikai_get_current_credit(key);
            snprintf(tmp, sizeof(tmp), "Credit: %u.%02u EUR\n", c / 100, c % 100);
            strncat(out, tmp, outLen - strlen(out) - 1);
        }
        snprintf(tmp, sizeof(tmp), "SK: %08" PRIX32 "\n", key->encryptionKey);
        strncat(out, tmp, outLen - strlen(out) - 1);
        snprintf(tmp, sizeof(tmp), "Bound: %s\n", !mikai_is_reset(key) ? "yes" : "no");
        strncat(out, tmp, outLen - strlen(out) - 1);
    }

    // Data di produzione (BCD)
    int dd = ((key->srix4k->eeprom[0x08][0] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][0] & 0x0F);
    int mm = ((key->srix4k->eeprom[0x08][1] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][1] & 0x0F);
    int yy = (key->srix4k->eeprom[0x08][3] & 0x0F) * 1000 +
             ((key->srix4k->eeprom[0x08][3] & 0xF0) >> 4) * 100 +
             ((key->srix4k->eeprom[0x08][2] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][2] & 0x0F);
    snprintf(tmp, sizeof(tmp), "Prod: %02d/%02d/%04d\n", dd, mm, yy);
    strncat(out, tmp, outLen - strlen(out) - 1);

    // Storico transazioni (ordinato, come nell'originale)
    strncat(out, "--- Transactions ---\n", outLen - strlen(out) - 1);
    uint8_t current = get_current_transaction_offset(key);
    for (uint8_t i = 0; i < 8; i++) {
        if (current == 7) current = 0;
        else current++;
        uint8_t *eb = key->srix4k->eeprom[0x34 + current];
        if (eb[0] == 0xFF && eb[1] == 0xFF && eb[2] == 0xFF && eb[3] == 0xFF) {
            snprintf(tmp, sizeof(tmp), "[%u] No transaction\n", i);
        } else {
            uint8_t td = eb[0] >> 3;
            uint8_t tm = ((eb[0] & 0x07) << 1) | ((eb[1] & 0x80) >> 7);
            uint16_t ty = 2000 + (eb[1] & 0x7F);
            uint16_t tc = (uint16_t)((eb[2] << 8) | eb[3]);
            snprintf(
                tmp, sizeof(tmp), "[%u] %02u/%02u/%04u  %u.%02u EUR\n", i, td, tm, ty, tc / 100, tc % 100
            );
        }
        strncat(out, tmp, outLen - strlen(out) - 1);
    }
}

int mikai_add_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year) {
    if (mikai_check_lock_id(key)) {
        Serial.println("[MIKAI] Lock ID active.");
        return -1;
    }
    if (mikai_is_reset(key)) {
        Serial.println("[MIKAI] Key not bound.");
        return -2;
    }
    if (cents < 5) {
        Serial.println("[MIKAI] Amount < 5 cents.");
        return -3;
    }

    uint16_t actual_credit = mikai_get_current_credit(key);
    uint16_t precedent_credit = actual_credit;
    uint8_t current = get_current_transaction_offset(key);

    while (cents >= 5) {
        precedent_credit = actual_credit;
        uint16_t step;
        if (cents >= 200) step = 200;
        else if (cents >= 100) step = 100;
        else if (cents >= 50) step = 50;
        else if (cents >= 20) step = 20;
        else if (cents >= 10) step = 10;
        else step = 5;
        cents -= step;
        actual_credit += step;

        if (current == 7) current = 0;
        else current++;

        uint8_t *eb = key->srix4k->eeprom[0x34 + current];
        eb[0] = (day << 3) | ((month & 0x0E) >> 1);
        eb[1] = (month << 7) | (year & 0x7F);
        eb[2] = actual_credit >> 8;
        eb[3] = (uint8_t)actual_credit;
        srix_flag_add(&key->srix4k->srixFlag, 0x34 + current);
    }

    // Block 0x21 - new credit
    key->srix4k->eeprom[0x21][1] = 0x00;
    key->srix4k->eeprom[0x21][2] = actual_credit >> 8;
    key->srix4k->eeprom[0x21][3] = (uint8_t)actual_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x21], 0x21);
    encode_decode_block(key->srix4k->eeprom[0x21]);
    key->srix4k->eeprom[0x21][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x21][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x21][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x21][3] ^= key->encryptionKey;
    srix_flag_add(&key->srix4k->srixFlag, 0x21);

    // Block 0x25 - mirror
    key->srix4k->eeprom[0x25][1] = 0x00;
    key->srix4k->eeprom[0x25][2] = actual_credit >> 8;
    key->srix4k->eeprom[0x25][3] = (uint8_t)actual_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x25], 0x25);
    encode_decode_block(key->srix4k->eeprom[0x25]);
    key->srix4k->eeprom[0x25][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x25][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x25][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x25][3] ^= key->encryptionKey;
    srix_flag_add(&key->srix4k->srixFlag, 0x25);

    // Block 0x23 - precedent credit
    key->srix4k->eeprom[0x23][1] = 0x00;
    key->srix4k->eeprom[0x23][2] = precedent_credit >> 8;
    key->srix4k->eeprom[0x23][3] = (uint8_t)precedent_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x23], 0x23);
    encode_decode_block(key->srix4k->eeprom[0x23]);
    srix_flag_add(&key->srix4k->srixFlag, 0x23);

    // Block 0x27 - mirror
    key->srix4k->eeprom[0x27][1] = 0x00;
    key->srix4k->eeprom[0x27][2] = precedent_credit >> 8;
    key->srix4k->eeprom[0x27][3] = (uint8_t)precedent_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x27], 0x27);
    encode_decode_block(key->srix4k->eeprom[0x27]);
    srix_flag_add(&key->srix4k->srixFlag, 0x27);

    // Block 0x3C - transaction pointer
    key->srix4k->eeprom[0x3C][1] = current;
    key->srix4k->eeprom[0x3C][2] = 0;
    key->srix4k->eeprom[0x3C][3] = 0;
    calculateBlockChecksum(key->srix4k->eeprom[0x3C], 0x3C);
    encode_decode_block(key->srix4k->eeprom[0x3C]);
    key->srix4k->eeprom[0x3C][1] ^= key->srix4k->eeprom[0x07][1];
    key->srix4k->eeprom[0x3C][2] ^= key->srix4k->eeprom[0x07][2];
    key->srix4k->eeprom[0x3C][3] ^= key->srix4k->eeprom[0x07][3];
    srix_flag_add(&key->srix4k->srixFlag, 0x3C);

    return 0;
}

int mikai_set_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year) {
    uint8_t dump21[SRIX_BLOCK_LENGTH];
    uint8_t dump34[9 * SRIX_BLOCK_LENGTH];
    memcpy(dump21, key->srix4k->eeprom[0x21], SRIX_BLOCK_LENGTH);
    memcpy(dump34, key->srix4k->eeprom[0x34], 9 * SRIX_BLOCK_LENGTH);

    key->srix4k->eeprom[0x21][0] = 0xC0;
    key->srix4k->eeprom[0x21][1] = 0x40;
    key->srix4k->eeprom[0x21][2] = 0xC0;
    key->srix4k->eeprom[0x21][3] = 0x80;
    key->srix4k->eeprom[0x21][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x21][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x21][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x21][3] ^= key->encryptionKey;
    memset(key->srix4k->eeprom[0x34], 0xFF, 9 * SRIX_BLOCK_LENGTH);

    if (mikai_add_cents(key, cents, day, month, year) < 0) {
        memcpy(key->srix4k->eeprom[0x21], dump21, SRIX_BLOCK_LENGTH);
        memcpy(key->srix4k->eeprom[0x34], dump34, 9 * SRIX_BLOCK_LENGTH);
        return -1;
    }
    return 0;
}

void mikai_import_vendor(struct mykey_t *key, const uint8_t block18[4], const uint8_t block19[4]) {
    memcpy(key->srix4k->eeprom[0x18], block18, SRIX_BLOCK_LENGTH);
    srix_flag_add(&key->srix4k->srixFlag, 0x18);
    memcpy(key->srix4k->eeprom[0x19], block19, SRIX_BLOCK_LENGTH);
    srix_flag_add(&key->srix4k->srixFlag, 0x19);

    /* Decifra 0x21 e 0x25 con la vecchia SK */
    key->srix4k->eeprom[0x21][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x21][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x21][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x21][3] ^= key->encryptionKey;

    key->srix4k->eeprom[0x25][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x25][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x25][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x25][3] ^= key->encryptionKey;

    /* Ricalcola la chiave con il nuovo vendor */
    calculateEncryptionKey(key);

    /* Ricicifra 0x21 e 0x25 con la nuova SK */
    key->srix4k->eeprom[0x21][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x21][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x21][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x21][3] ^= key->encryptionKey;
    srix_flag_add(&key->srix4k->srixFlag, 0x21);

    key->srix4k->eeprom[0x25][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x25][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x25][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x25][3] ^= key->encryptionKey;
    srix_flag_add(&key->srix4k->srixFlag, 0x25);

    /* Mirror 18/19 → 1C/1D */
    memcpy(key->srix4k->eeprom[0x1C], block18, SRIX_BLOCK_LENGTH);
    memcpy(key->srix4k->eeprom[0x1D], block19, SRIX_BLOCK_LENGTH);
    encode_decode_block(key->srix4k->eeprom[0x1C]);
    encode_decode_block(key->srix4k->eeprom[0x1D]);
    calculateBlockChecksum(key->srix4k->eeprom[0x1C], 0x1C);
    calculateBlockChecksum(key->srix4k->eeprom[0x1D], 0x1D);
    encode_decode_block(key->srix4k->eeprom[0x1C]);
    encode_decode_block(key->srix4k->eeprom[0x1D]);
    srix_flag_add(&key->srix4k->srixFlag, 0x1C);
    srix_flag_add(&key->srix4k->srixFlag, 0x1D);
}

int mikai_export_vendor(struct mykey_t *key, uint8_t buffer[8]) {
    if (mikai_is_reset(key)) return -1;
    memcpy(buffer, key->srix4k->eeprom[0x18], SRIX_BLOCK_LENGTH * 2);
    return 0;
}

void mikai_reset_key(struct mykey_t *key) {
    for (uint8_t i = 0x10; i < SRIX4K_BLOCKS; i++) {
        uint8_t block[4] = {0, 0, 0, 0};
        switch (i) {
            case 0x10:
            case 0x14:
            case 0x3F:
            case 0x43: {
                block[1] = key->srix4k->eeprom[0x07][0];
                int dd =
                    ((key->srix4k->eeprom[0x08][0] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][0] & 0x0F);
                int mm =
                    ((key->srix4k->eeprom[0x08][1] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][1] & 0x0F);
                int yy = (key->srix4k->eeprom[0x08][3] & 0x0F) * 1000 +
                         ((key->srix4k->eeprom[0x08][3] & 0xF0) >> 4) * 100 +
                         ((key->srix4k->eeprom[0x08][2] & 0xF0) >> 4) * 10 +
                         (key->srix4k->eeprom[0x08][2] & 0x0F);
                uint32_t el = days_difference(dd, mm, yy);
                block[2] = (uint8_t)((el / 1000 % 10) * 16 + (el / 100 % 10));
                block[3] = (uint8_t)((el / 10 % 10) * 16 + (el % 10));
                calculateBlockChecksum(block, i);
                break;
            }
            case 0x11:
            case 0x15:
            case 0x40:
            case 0x44:
                block[1] = key->srix4k->eeprom[0x07][1];
                block[2] = key->srix4k->eeprom[0x07][2];
                block[3] = key->srix4k->eeprom[0x07][3];
                calculateBlockChecksum(block, i);
                break;
            case 0x22:
            case 0x26:
            case 0x51:
            case 0x55:
                block[1] = key->srix4k->eeprom[0x08][2];
                block[2] = key->srix4k->eeprom[0x08][1];
                block[3] = key->srix4k->eeprom[0x08][3];
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;
            case 0x12:
            case 0x16:
            case 0x41:
            case 0x45:
                block[1] = 0x00;
                block[2] = 0x00;
                block[3] = 0x01;
                calculateBlockChecksum(block, i);
                break;
            case 0x13:
            case 0x17:
            case 0x42:
            case 0x46:
                block[1] = 0x04;
                block[2] = 0x00;
                block[3] = 0x13;
                calculateBlockChecksum(block, i);
                break;
            case 0x18:
            case 0x1C:
            case 0x47:
            case 0x4B:
                block[1] = 0x00;
                block[2] = 0xFE;
                block[3] = 0xDC;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;
            case 0x1D:
            case 0x48:
            case 0x4C:
                block[1] = 0x00;
                block[2] = 0x01;
                block[3] = 0x23;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;
            case 0x19:
                block[1] = 0x00;
                block[2] = 0x01;
                block[3] = 0x23;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                calculateEncryptionKey(key);
                break; // recalc after vendor block
            case 0x21:
            case 0x25:
                block[1] = 0x00;
                block[2] = 0x00;
                block[3] = 0x00;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                block[0] ^= key->encryptionKey >> 24;
                block[1] ^= key->encryptionKey >> 16;
                block[2] ^= key->encryptionKey >> 8;
                block[3] ^= key->encryptionKey;
                break;
            case 0x20:
            case 0x24:
            case 0x4F:
            case 0x53:
                block[1] = 0x01;
                block[2] = 0x00;
                block[3] = 0x00;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;
            case 0x1A:
            case 0x1B:
            case 0x1E:
            case 0x1F:
            case 0x23:
            case 0x27:
            case 0x49:
            case 0x4A:
            case 0x4D:
            case 0x4E:
            case 0x50:
            case 0x52:
            case 0x54:
            case 0x56:
                block[1] = 0x00;
                block[2] = 0x00;
                block[3] = 0x00;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;
            default: block[0] = block[1] = block[2] = block[3] = 0xFF; break;
        }
        if (memcmp(block, key->srix4k->eeprom[i], 4) != 0) {
            Serial.printf("[MIKAI] Resetting block %02X\n", i);
            memcpy(key->srix4k->eeprom[i], block, 4);
            srix_flag_add(&key->srix4k->srixFlag, i);
        }
    }
    Serial.println("[MIKAI] Key reset done.");
}

int mikai_write_modified_blocks(struct mykey_t *key, Arduino_PN532_SRIX *nfc) {
    // Block 6 first (OTP-reset order, same as original)
    if (srix_flag_get(&key->srix4k->srixFlag, 0x06)) {
        write_block(nfc, key->srix4k, 0x06);
        srix_flag_remove(&key->srix4k->srixFlag, 0x06);
    }
    for (uint8_t i = 0; i < SRIX4K_BLOCKS; i++) {
        if (srix_flag_get(&key->srix4k->srixFlag, i)) write_block(nfc, key->srix4k, i);
    }
    key->srix4k->srixFlag = srix_flag_init();
    return 0;
}

bool mikai_has_pending_writes(struct mykey_t *key) { return srix_flag_isModified(&key->srix4k->srixFlag); }

// Non usate
void mikai_export_dump(struct mykey_t *key, uint64_t *uid_out, uint8_t eeprom_out[SRIX4K_BYTES]) {
    memcpy(eeprom_out, key->srix4k->eeprom, SRIX4K_BYTES);
    *uid_out = key->srix4k->uid;
}

void mikai_modify_block(struct mykey_t *key, const uint8_t block[4], uint8_t blockNum) {
    if (blockNum < 0x10 || blockNum > 0x7F) return;
    memcpy(key->srix4k->eeprom[blockNum], block, 4);
    srix_flag_add(&key->srix4k->srixFlag, blockNum);
}

static bool srix_decrease_block6(struct srix_t *target, uint32_t toDecrease) {
    if (toDecrease == 0) return true;
    uint32_t b6 = ((uint32_t)target->eeprom[0x06][3] << 24) | ((uint32_t)target->eeprom[0x06][2] << 16) |
                  ((uint32_t)target->eeprom[0x06][1] << 8) | (uint32_t)target->eeprom[0x06][0];
    if (b6 < toDecrease) return false;
    b6 -= toDecrease;
    target->eeprom[0x06][0] = (uint8_t)(b6);
    target->eeprom[0x06][1] = (uint8_t)(b6 >> 8);
    target->eeprom[0x06][2] = (uint8_t)(b6 >> 16);
    target->eeprom[0x06][3] = (uint8_t)(b6 >> 24);
    srix_flag_add(&target->srixFlag, 0x06);
    return true;
}

static int srix_reset_otp_internal(struct srix_t *target) {
    uint8_t reset[5 * SRIX_BLOCK_LENGTH];
    memset(reset, 0xFF, sizeof(reset));
    if (memcmp(target->eeprom[0x00], reset, sizeof(reset)) != 0) {
        if (srix_decrease_block6(target, 0x00200000)) {
            memset(target->eeprom[0x00], 0xFF, 5 * SRIX_BLOCK_LENGTH);
            for (uint8_t b = 0x00; b <= 0x04; b++) srix_flag_add(&target->srixFlag, b);
        } else {
            Serial.println("[MIKAI] Unable to decrease block 0x06 for OTP reset.");
            return -1;
        }
    }
    return 0;
}

void mikai_reset_otp(struct mykey_t *key) {
    if (srix_reset_otp_internal(key->srix4k) >= 0) {
        calculateEncryptionKey(key);
        Serial.println("[MIKAI] OTP reset done.");
    }
}
