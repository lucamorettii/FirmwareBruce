/**
 * @file MikaiLogic.cpp
 * @brief Implementazione della logica core per i tag SRIX4K (MyKey / Mikai).
 *
 * Questo file contiene:
 *   - Il bridge NFC (attesa tag, selezione, lettura/scrittura blocchi con retry)
 *   - Le funzioni crittografiche interne (permutazione bit, checksum, chiave)
 *   - L'API pubblica mikai_* dichiarata in MikaiLogic.h
 *
 * Convenzione di log: tutti i messaggi seriali usano il prefisso "[MIKAI]".
 */

#include "MikaiLogic.h"
#include <Arduino.h>

// ─── Stato del modulo ─────────────────────────────────────────────────────────

/// Driver PN532 inizializzato senza pin CS/IRQ (I²C).
Arduino_PN532_SRIX nfc(255, 255);

/// Immagine RAM del tag corrente.
struct srix_t srix;

/// Handle di lavoro che accoppia il tag alla sua chiave di sessione.
struct mykey_t srixKey = {&srix, 0};

// ─── Bridge NFC (funzioni statiche) ──────────────────────────────────────────

/**
 * @brief Mette il PN532 in modalità ISO14443B2SR e aspetta che un tag risponda.
 *
 * Esegue fino a 60 tentativi (~6 s) prima di rinunciare.
 * @return true se un tag è stato selezionato con successo.
 */
static bool nfc_wait_and_select(Arduino_PN532_SRIX *nfc) {
    if (!nfc->SRIX_init()) return false;

    for (int attempt = 0; attempt < 60; attempt++) {
        if (nfc->SRIX_initiate_select()) return true;
        delay(100);
    }
    return false;
}

/**
 * @brief Ri-inizializza il PN532 e seleziona nuovamente il tag dopo un errore.
 *
 * Usata internamente da read_block e write_block per il meccanismo di retry.
 */
static void nfc_reselect(Arduino_PN532_SRIX *nfc) {
    nfc->SRIX_init();
    for (int i = 0; i < 60; i++) {
        if (nfc->SRIX_initiate_select()) return;
        delay(100);
    }
}

/**
 * @brief Legge un blocco con retry automatico fino al successo.
 *
 * Se la lettura fallisce, chiede all'utente di riposizionare il tag
 * e ritenta dopo aver ri-selezionato il tag.
 * @param rx      Buffer di destinazione (4 byte).
 * @param blockNum Numero del blocco da leggere (0x00–0x7F).
 */
static void read_block(Arduino_PN532_SRIX *nfc, uint8_t rx[4], uint8_t blockNum) {
    while (!nfc->SRIX_read_block(blockNum, rx)) {
        Serial.printf("[MIKAI] Block %02X read failed - reposition tag\n", blockNum);
        nfc_reselect(nfc);
    }
}

/**
 * @brief Scrive un blocco e verifica immediatamente la scrittura; ritenta in caso di mismatch.
 *
 * Dopo ogni scrittura rilege il blocco e confronta byte per byte.
 * In caso di differenza ri-seleziona il tag e riprova.
 * @param target   Struttura srix_t che contiene i dati aggiornati.
 * @param blockNum Numero del blocco da scrivere.
 */
static void write_block(Arduino_PN532_SRIX *nfc, struct srix_t *target, uint8_t blockNum) {
    Serial.printf("[MIKAI] Writing block %02X...\n", blockNum);
    while (true) {
        nfc->SRIX_write_block(blockNum, target->eeprom[blockNum]);

        uint8_t check[4];
        read_block(nfc, check, blockNum);

        if (memcmp(target->eeprom[blockNum], check, 4) == 0) return; // verifica ok

        Serial.printf("[MIKAI] Block %02X verify failed - retrying\n", blockNum);
        nfc_reselect(nfc);
    }
}

// ─── Crittografia e codifica interna ─────────────────────────────────────────

/**
 * @brief Permutazione a 2 bit per blocco (encode/decode simmetrico).
 *
 * Riscrive i 4 byte in-place estraendo i 2 bit più significativi da ciascun
 * byte di input e riorientandoli su 4 byte di output. L'operazione è la
 * propria inversa (applicarla due volte = identità).
 */
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

/**
 * @brief Calcola e scrive il byte di checksum in block[0].
 *
 * Il checksum è: 0xFF − blockNum − somma di tutte le nibble di block[1..3].
 * Viene scritto direttamente nel byte block[0] del buffer.
 */
static void calculateBlockChecksum(uint8_t block[4], uint8_t blockNum) {
    block[0] = 0xFF - blockNum - (block[3] & 0x0F) - ((block[3] >> 4) & 0x0F) - (block[2] & 0x0F) -
               ((block[2] >> 4) & 0x0F) - (block[1] & 0x0F) - ((block[1] >> 4) & 0x0F);
}

/**
 * @brief Calcola il numero di giorni giuliani dalla data di produzione.
 *
 * Formula di Zeller ridotta, azzerata al 1° gennaio 2000 (giorno 0 = 2000-01-01).
 * Usata nei blocchi di reset per codificare la data di produzione.
 */
static uint32_t days_difference(int day, int month, int year) {
    if (month < 3) {
        year--;
        month += 12;
    }
    return (uint32_t)(year * 365 + year / 4 - year / 100 + year / 400 + (month * 153 + 3) / 5 + day - 728692);
}

/**
 * @brief Calcola la chiave di sessione (encryptionKey) del tag.
 *
 * La chiave dipende dall'UID del tag e dai blocchi vendor (0x18, 0x19)
 * che identificano il gestore del sistema. Viene memorizzata in key->encryptionKey.
 *
 * Algoritmo:
 *   1. Legge il contatore OTP dal blocco 0x06 (complemento a 1 + 1).
 *   2. Decodifica i blocchi 0x18 e 0x19 con encode_decode_block.
 *   3. Costruisce una master-key da UID × (parte dei blocchi decodificati + 1).
 *   4. La chiave finale è masterKey × OTP.
 */
static void calculateEncryptionKey(struct mykey_t *key) {
    // Legge il valore OTP dal blocco 0x06 (formato little-endian, complemento a 1 + 1)
    uint32_t otp = (((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][3]) << 24) |
                    ((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][2]) << 16) |
                    ((uint32_t)(0xFFu - key->srix4k->eeprom[0x06][1]) << 8) |
                    (uint32_t)(0xFFu - key->srix4k->eeprom[0x06][0])) +
                   1u;

    // Decodifica i blocchi vendor per estrarre i parametri della master-key
    uint8_t b18[4], b19[4];
    memcpy(b18, key->srix4k->eeprom[0x18], 4);
    memcpy(b19, key->srix4k->eeprom[0x19], 4);
    encode_decode_block(b18);
    encode_decode_block(b19);

    // Combina UID con i byte vendor per ottenere la master-key
    uint32_t masterKey =
        (uint32_t)(key->srix4k->uid * (uint64_t)((((uint32_t)b18[2] << 24) | ((uint32_t)b18[3] << 16) |
                                                  ((uint32_t)b19[2] << 8) | (uint32_t)b19[3]) +
                                                 1u));

    key->encryptionKey = masterKey * otp;
}

/**
 * @brief Restituisce l'indice (0..7) dell'ultima transazione scritta.
 *
 * Il puntatore è memorizzato nel blocco 0x3C, cifrato con la chiave
 * del blocco 0x07. Se il blocco è vergine (0xFF) restituisce 7 (indice iniziale).
 */
static uint8_t get_current_transaction_offset(struct mykey_t *key) {
    // Blocco vergine: nessuna transazione ancora scritta
    if (key->srix4k->eeprom[0x3C][1] == 0xFF) return 0x07;

    // Decifra il puntatore XORando con i byte di 0x07
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

// ─── API pubblica mikai_* ─────────────────────────────────────────────────────

/**
 * @brief Legge un tag SRIX4K e popola la struttura mykey_t.
 *
 * Passi:
 *   1. Attende e seleziona il tag.
 *   2. Legge l'UID e verifica che sia un SRIX4K (header 0xD0, 0x02).
 *   3. Legge tutti i 128 blocchi EEPROM.
 *   4. Calcola la chiave di sessione.
 *
 * @return true se la lettura è andata a buon fine.
 */
bool mikai_read_tag(struct mykey_t *key, Arduino_PN532_SRIX *nfc) {
    memset(key->srix4k, 0, sizeof(struct srix_t));
    key->srix4k->srixFlag = srix_flag_init();
    key->encryptionKey = 0;

    Serial.println("[MIKAI] Waiting for SRIX4K tag...");
    if (!nfc_wait_and_select(nfc)) {
        Serial.println("[MIKAI] No tag found.");
        return false;
    }

    // Lettura e validazione UID
    uint8_t uid_bytes[8];
    if (!nfc->SRIX_get_uid(uid_bytes)) {
        Serial.println("[MIKAI] UID read failed.");
        return false;
    }
    if (uid_bytes[7] != 0xD0 || uid_bytes[6] != 0x02) {
        Serial.println("[MIKAI] Not an SRIX4K tag (bad UID header).");
        return false;
    }

    // Assembla l'UID a 64 bit dal buffer little-endian del PN532
    key->srix4k->uid = ((uint64_t)uid_bytes[7] << 56) | ((uint64_t)uid_bytes[6] << 48) |
                       ((uint64_t)uid_bytes[5] << 40) | ((uint64_t)uid_bytes[4] << 32) |
                       ((uint64_t)uid_bytes[3] << 24) | ((uint64_t)uid_bytes[2] << 16) |
                       ((uint64_t)uid_bytes[1] << 8) | (uint64_t)uid_bytes[0];
    Serial.printf("[MIKAI] UID: %016llX\n", key->srix4k->uid);

    // Lettura completa dell'EEPROM (128 blocchi × 4 byte)
    Serial.printf("[MIKAI] Reading %u blocks...\n", SRIX4K_BLOCKS);
    for (uint8_t i = 0; i < SRIX4K_BLOCKS; i++) read_block(nfc, key->srix4k->eeprom[i], i);

    calculateEncryptionKey(key);
    Serial.printf("[MIKAI] SK: %08lX\n", key->encryptionKey);
    return true;
}

/**
 * @brief Verifica se il tag è nello stato di reset (vendor non ancora impostato).
 *
 * Controlla i valori dei blocchi 0x18 e 0x19 contro le costanti di default
 * di fabbrica.
 */
bool mikai_is_reset(struct mykey_t *key) {
    return key->srix4k->eeprom[0x18][0] == 0x8F && key->srix4k->eeprom[0x18][1] == 0xCD &&
           key->srix4k->eeprom[0x18][2] == 0x0F && key->srix4k->eeprom[0x18][3] == 0x48 &&
           key->srix4k->eeprom[0x19][0] == 0xC0 && key->srix4k->eeprom[0x19][1] == 0x82 &&
           key->srix4k->eeprom[0x19][2] == 0x00 && key->srix4k->eeprom[0x19][3] == 0x07;
}

/**
 * @brief Controlla se il Lock-ID è attivo (tag in sola lettura).
 *
 * Decifra il blocco 0x21 con la chiave di sessione e ricalcola il checksum:
 * se il checksum salvato non coincide con quello calcolato, il Lock-ID è attivo.
 */
bool mikai_check_lock_id(struct mykey_t *key) {
    uint8_t cc[4];
    cc[0] = key->srix4k->eeprom[0x21][0] ^ (key->encryptionKey >> 24);
    cc[1] = key->srix4k->eeprom[0x21][1] ^ (key->encryptionKey >> 16);
    cc[2] = key->srix4k->eeprom[0x21][2] ^ (key->encryptionKey >> 8);
    cc[3] = key->srix4k->eeprom[0x21][3] ^ key->encryptionKey;
    encode_decode_block(cc);

    uint8_t saved = cc[0];
    calculateBlockChecksum(cc, 0x21);

    // Lock-ID attivo: byte 5[3] == 0x7F e checksum non corrisponde
    return (key->srix4k->eeprom[0x05][3] == 0x7F && saved != cc[0]);
}

/**
 * @brief Restituisce il credito corrente in centesimi.
 *
 * Decifra il blocco 0x21 con la chiave di sessione e legge i byte 2–3
 * che contengono il credito in centesimi (big-endian).
 */
uint16_t mikai_get_current_credit(struct mykey_t *key) {
    uint8_t cc[4];
    cc[0] = key->srix4k->eeprom[0x21][0] ^ (key->encryptionKey >> 24);
    cc[1] = key->srix4k->eeprom[0x21][1] ^ (key->encryptionKey >> 16);
    cc[2] = key->srix4k->eeprom[0x21][2] ^ (key->encryptionKey >> 8);
    cc[3] = key->srix4k->eeprom[0x21][3] ^ key->encryptionKey;
    encode_decode_block(cc);

    return (uint16_t)((cc[2] << 8) | cc[3]);
}

/**
 * @brief Compone una stringa leggibile con le informazioni del tag.
 *
 * Include: Lock-ID, credito, chiave di sessione, stato di binding,
 * data di produzione (BCD) e lo storico delle ultime 8 transazioni
 * in ordine cronologico.
 *
 * @param out    Buffer di destinazione.
 * @param outLen Dimensione massima del buffer.
 */
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

    // Data di produzione codificata in BCD nei byte del blocco 0x08
    int dd = ((key->srix4k->eeprom[0x08][0] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][0] & 0x0F);
    int mm = ((key->srix4k->eeprom[0x08][1] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][1] & 0x0F);
    int yy = (key->srix4k->eeprom[0x08][3] & 0x0F) * 1000 +
             ((key->srix4k->eeprom[0x08][3] & 0xF0) >> 4) * 100 +
             ((key->srix4k->eeprom[0x08][2] & 0xF0) >> 4) * 10 + (key->srix4k->eeprom[0x08][2] & 0x0F);
    snprintf(tmp, sizeof(tmp), "Prod: %02d/%02d/%04d\n", dd, mm, yy);
    strncat(out, tmp, outLen - strlen(out) - 1);

    // Storico transazioni: si parte dall'offset corrente e si scorre in senso
    // crescente per mostrare le transazioni in ordine cronologico
    strncat(out, "--- Transactions ---\n", outLen - strlen(out) - 1);
    uint8_t current = get_current_transaction_offset(key);
    for (uint8_t i = 0; i < 8; i++) {
        if (current == 7) current = 0;
        else current++;

        uint8_t *eb = key->srix4k->eeprom[0x34 + current];
        if (eb[0] == 0xFF && eb[1] == 0xFF && eb[2] == 0xFF && eb[3] == 0xFF) {
            snprintf(tmp, sizeof(tmp), "[%u] No transaction\n", i);
        } else {
            // Decodifica data e importo dal formato compresso del blocco transazione
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

/**
 * @brief Aggiunge centesimi al credito del tag, spezzando l'importo in step canonici.
 *
 * Gli step ammessi sono: 200, 100, 50, 20, 10, 5 centesimi (importo minimo: 5).
 * Per ogni step viene creata una voce nel log delle transazioni (blocchi 0x34–0x3C).
 * I blocchi 0x21/0x25 (credito corrente) e 0x23/0x27 (credito precedente)
 * vengono aggiornati e cifrati con la chiave di sessione.
 *
 * @return 0 ok, -1 Lock-ID attivo, -2 tag non bound, -3 importo < 5 cent.
 */
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

    // Spezza l'importo in step canonici e registra ogni step come transazione
    while (cents >= 5) {
        precedent_credit = actual_credit;

        // Sceglie lo step più grande possibile
        uint16_t step;
        if (cents >= 200) step = 200;
        else if (cents >= 100) step = 100;
        else if (cents >= 50) step = 50;
        else if (cents >= 20) step = 20;
        else if (cents >= 10) step = 10;
        else step = 5;

        cents -= step;
        actual_credit += step;

        // Avanza circolarmente nel ring buffer delle transazioni (8 slot: 0..7)
        if (current == 7) current = 0;
        else current++;

        // Scrive il record di transazione nel blocco 0x34+current
        // Formato: [dd|mmH] [mmL|yyY] [creditH] [creditL]
        uint8_t *eb = key->srix4k->eeprom[0x34 + current];
        eb[0] = (day << 3) | ((month & 0x0E) >> 1);
        eb[1] = (month << 7) | (year & 0x7F);
        eb[2] = actual_credit >> 8;
        eb[3] = (uint8_t)actual_credit;
        srix_flag_add(&key->srix4k->srixFlag, 0x34 + current);
    }

    // ── Blocco 0x21: credito corrente (cifrato) ───────────────────────────────
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

    // ── Blocco 0x25: mirror di 0x21 (cifrato) ────────────────────────────────
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

    // ── Blocco 0x23: credito precedente (non cifrato) ─────────────────────────
    key->srix4k->eeprom[0x23][1] = 0x00;
    key->srix4k->eeprom[0x23][2] = precedent_credit >> 8;
    key->srix4k->eeprom[0x23][3] = (uint8_t)precedent_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x23], 0x23);
    encode_decode_block(key->srix4k->eeprom[0x23]);
    srix_flag_add(&key->srix4k->srixFlag, 0x23);

    // ── Blocco 0x27: mirror di 0x23 ───────────────────────────────────────────
    key->srix4k->eeprom[0x27][1] = 0x00;
    key->srix4k->eeprom[0x27][2] = precedent_credit >> 8;
    key->srix4k->eeprom[0x27][3] = (uint8_t)precedent_credit;
    calculateBlockChecksum(key->srix4k->eeprom[0x27], 0x27);
    encode_decode_block(key->srix4k->eeprom[0x27]);
    srix_flag_add(&key->srix4k->srixFlag, 0x27);

    // ── Blocco 0x3C: puntatore all'ultima transazione (cifrato con blocco 0x07) ─
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

/**
 * @brief Imposta il credito a un valore esatto, azzerando lo storico precedente.
 *
 * Salva e ripristina i blocchi 0x21 e 0x34–0x3C se mikai_add_cents fallisce,
 * garantendo l'atomicità dell'operazione in RAM.
 *
 * @return 0 ok, -1 se mikai_add_cents ha restituito errore.
 */
int mikai_set_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year) {
    // Salva lo stato corrente per il rollback in caso di errore
    uint8_t dump21[SRIX_BLOCK_LENGTH];
    uint8_t dump34[9 * SRIX_BLOCK_LENGTH];
    memcpy(dump21, key->srix4k->eeprom[0x21], SRIX_BLOCK_LENGTH);
    memcpy(dump34, key->srix4k->eeprom[0x34], 9 * SRIX_BLOCK_LENGTH);

    // Forza il credito a 0 e azzera il log delle transazioni
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
        // Rollback: ripristina lo stato precedente
        memcpy(key->srix4k->eeprom[0x21], dump21, SRIX_BLOCK_LENGTH);
        memcpy(key->srix4k->eeprom[0x34], dump34, 9 * SRIX_BLOCK_LENGTH);
        return -1;
    }
    return 0;
}

/**
 * @brief Sostituisce i blocchi vendor (0x18, 0x19) e ricalcola la cifratura.
 *
 * Passi:
 *   1. Salva i nuovi blocchi vendor.
 *   2. Decifra 0x21 e 0x25 con la vecchia chiave.
 *   3. Ricalcola la chiave con il nuovo vendor.
 *   4. Ri-cifra 0x21 e 0x25 con la nuova chiave.
 *   5. Scrive i mirror cifrati in 0x1C e 0x1D.
 */
void mikai_import_vendor(struct mykey_t *key, const uint8_t block18[4], const uint8_t block19[4]) {
    // 1. Salva i nuovi blocchi vendor
    memcpy(key->srix4k->eeprom[0x18], block18, SRIX_BLOCK_LENGTH);
    srix_flag_add(&key->srix4k->srixFlag, 0x18);
    memcpy(key->srix4k->eeprom[0x19], block19, SRIX_BLOCK_LENGTH);
    srix_flag_add(&key->srix4k->srixFlag, 0x19);

    // 2. Decifra 0x21 e 0x25 con la chiave VECCHIA
    key->srix4k->eeprom[0x21][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x21][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x21][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x21][3] ^= key->encryptionKey;

    key->srix4k->eeprom[0x25][0] ^= key->encryptionKey >> 24;
    key->srix4k->eeprom[0x25][1] ^= key->encryptionKey >> 16;
    key->srix4k->eeprom[0x25][2] ^= key->encryptionKey >> 8;
    key->srix4k->eeprom[0x25][3] ^= key->encryptionKey;

    // 3. Ricalcola la chiave con il nuovo vendor
    calculateEncryptionKey(key);

    // 4. Ri-cifra 0x21 e 0x25 con la chiave NUOVA
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

    // 5. Aggiorna i mirror cifrati 0x1C e 0x1D (doppia codifica + checksum)
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

/**
 * @brief Esporta gli 8 byte raw dei blocchi vendor (0x18 e 0x19).
 *
 * @param buffer Destinazione: 8 byte (4 per 0x18 + 4 per 0x19).
 * @return 0 ok, -1 se il tag è in stato di reset (nessun vendor impostato).
 */
int mikai_export_vendor(struct mykey_t *key, uint8_t buffer[8]) {
    if (mikai_is_reset(key)) return -1;
    memcpy(buffer, key->srix4k->eeprom[0x18], SRIX_BLOCK_LENGTH * 2);
    return 0;
}

/**
 * @brief Riporta il tag allo stato di reset di fabbrica.
 *
 * Scrive il valore di default per ogni blocco da 0x10 a 0x7F,
 * ricalcolando checksum e permutazione dove necessario.
 * I blocchi non gestiti esplicitamente vengono impostati a 0xFF.
 *
 * Nota: dopo aver scritto il blocco 0x19 (vendor reset) viene ricalcolata
 * la chiave di sessione, poiché dipende dai blocchi vendor.
 */
void mikai_reset_key(struct mykey_t *key) {
    for (uint8_t i = 0x10; i < SRIX4K_BLOCKS; i++) {
        uint8_t block[4] = {0, 0, 0, 0};

        switch (i) {
            // Blocchi con data di produzione in giorni BCD
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

            // Blocchi con byte di configurazione dal blocco 0x07
            case 0x11:
            case 0x15:
            case 0x40:
            case 0x44:
                block[1] = key->srix4k->eeprom[0x07][1];
                block[2] = key->srix4k->eeprom[0x07][2];
                block[3] = key->srix4k->eeprom[0x07][3];
                calculateBlockChecksum(block, i);
                break;

            // Blocchi con data di produzione dal blocco 0x08 (codifica diversa)
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

            // Blocchi con valore fisso 0x0001
            case 0x12:
            case 0x16:
            case 0x41:
            case 0x45:
                block[1] = 0x00;
                block[2] = 0x00;
                block[3] = 0x01;
                calculateBlockChecksum(block, i);
                break;

            // Blocchi con valore fisso 0x0413
            case 0x13:
            case 0x17:
            case 0x42:
            case 0x46:
                block[1] = 0x04;
                block[2] = 0x00;
                block[3] = 0x13;
                calculateBlockChecksum(block, i);
                break;

            // Blocchi vendor default (0xFEDC) con permutazione
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

            // Mirror vendor default (0x0123) con permutazione
            case 0x1D:
            case 0x48:
            case 0x4C:
                block[1] = 0x00;
                block[2] = 0x01;
                block[3] = 0x23;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;

            // Blocco 0x19: vendor default
            case 0x19:
                block[1] = 0x00;
                block[2] = 0x01;
                block[3] = 0x23;
                calculateBlockChecksum(block, i);
                encode_decode_block(block);
                break;

            // Blocchi credito (0x00) cifrati con la chiave di sessione
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

            // Blocchi con valore fisso 0x0100 + permutazione
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

            // Blocchi vari azzerati con permutazione
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

            // Tutti gli altri blocchi (transazioni, ecc.): 0xFFFFFFFF
            default: block[0] = block[1] = block[2] = block[3] = 0xFF; break;
        }

        // Scrive il blocco solo se differisce da quello attuale
        if (memcmp(block, key->srix4k->eeprom[i], 4) != 0) {
            Serial.printf("[MIKAI] Resetting block %02X\n", i);
            memcpy(key->srix4k->eeprom[i], block, 4);
            srix_flag_add(&key->srix4k->srixFlag, i);
        }
    }

    // calculateEncryptionKey(key);
    Serial.println("[MIKAI] Key reset done.");
}

/**
 * @brief Scrive sul tag fisico tutti i blocchi marcati come modificati.
 *
 * Il blocco 0x06 (OTP counter) viene scritto per primo per rispettare
 * l'ordine di scrittura atteso dal firmware del tag.
 * Al termine, il flag-set viene azzerato.
 *
 * @return 0 sempre (gli errori di scrittura causano retry interni in write_block).
 */
int mikai_write_modified_blocks(struct mykey_t *key, Arduino_PN532_SRIX *nfc) {
    // Il blocco OTP (0x06) deve essere scritto prima degli altri
    if (srix_flag_get(&key->srix4k->srixFlag, 0x06)) {
        write_block(nfc, key->srix4k, 0x06);
        srix_flag_remove(&key->srix4k->srixFlag, 0x06);
    }

    // Scrive tutti gli altri blocchi modificati in ordine crescente
    for (uint8_t i = 0; i < SRIX4K_BLOCKS; i++) {
        if (srix_flag_get(&key->srix4k->srixFlag, i)) write_block(nfc, key->srix4k, i);
    }

    key->srix4k->srixFlag = srix_flag_init();
    return 0;
}

/** @brief Restituisce true se ci sono blocchi in attesa di scrittura sul tag. */
bool mikai_has_pending_writes(struct mykey_t *key) { return srix_flag_isModified(&key->srix4k->srixFlag); }

// ─── Funzioni ausiliarie (non usate dal menu corrente) ────────────────────────

/**
 * @brief Esporta una copia dell'intera EEPROM e l'UID del tag.
 * @param uid_out      Destinazione per l'UID a 64 bit.
 * @param eeprom_out   Buffer di almeno SRIX4K_BYTES byte.
 */
void mikai_export_dump(struct mykey_t *key, uint64_t *uid_out, uint8_t eeprom_out[SRIX4K_BYTES]) {
    memcpy(eeprom_out, key->srix4k->eeprom, SRIX4K_BYTES);
    *uid_out = key->srix4k->uid;
}

/**
 * @brief Sovrascrive un blocco arbitrario nell'immagine RAM e lo marca come modificato.
 *
 * Intervallo valido: blocchi 0x10..0x7F (i blocchi 0x00–0x0F sono di sistema).
 */
void mikai_modify_block(struct mykey_t *key, const uint8_t block[4], uint8_t blockNum) {
    if (blockNum < 0x10 || blockNum > 0x7F) return;
    memcpy(key->srix4k->eeprom[blockNum], block, 4);
    srix_flag_add(&key->srix4k->srixFlag, blockNum);
}

/**
 * @brief Decrementa il contatore OTP del blocco 0x06 del valore @p toDecrease.
 *
 * Il blocco 0x06 è un contatore monotono non-resettabile: ogni decremento
 * è irreversibile. Utilizzato solo da srix_reset_otp_internal.
 *
 * @return true se il decremento è riuscito (saldo sufficiente).
 */
static bool srix_decrease_block6(struct srix_t *target, uint32_t toDecrease) {
    if (toDecrease == 0) return true;

    // Legge il contatore OTP in little-endian
    uint32_t b6 = ((uint32_t)target->eeprom[0x06][3] << 24) | ((uint32_t)target->eeprom[0x06][2] << 16) |
                  ((uint32_t)target->eeprom[0x06][1] << 8) | (uint32_t)target->eeprom[0x06][0];

    if (b6 < toDecrease) return false;
    b6 -= toDecrease;

    // Riscrive in little-endian
    target->eeprom[0x06][0] = (uint8_t)(b6);
    target->eeprom[0x06][1] = (uint8_t)(b6 >> 8);
    target->eeprom[0x06][2] = (uint8_t)(b6 >> 16);
    target->eeprom[0x06][3] = (uint8_t)(b6 >> 24);
    srix_flag_add(&target->srixFlag, 0x06);
    return true;
}

/**
 * @brief Azzera i blocchi OTP (0x00–0x04) se non sono già a 0xFF.
 *
 * Per eseguire il reset OTP è necessario decrementare il blocco 0x06
 * di 0x00200000 unità (costo fisso del reset imposto dall'hardware SRIX4K).
 *
 * @return 0 se riuscito, -1 se il contatore OTP non ha saldo sufficiente.
 */
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

/**
 * @brief Azzera i blocchi OTP e ricalcola la chiave di sessione.
 *
 * Chiama srix_reset_otp_internal e, se riuscito, aggiorna encryptionKey
 * poiché il valore OTP entra nel calcolo della chiave.
 */
void mikai_reset_otp(struct mykey_t *key) {
    if (srix_reset_otp_internal(key->srix4k) >= 0) {
        calculateEncryptionKey(key);
        Serial.println("[MIKAI] OTP reset done.");
    }
}
