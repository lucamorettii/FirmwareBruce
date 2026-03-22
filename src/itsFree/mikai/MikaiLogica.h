/**
 * @file MikaiLogica.h
 * @brief Strutture dati, costanti e API della logica core per i tag SRIX4K (MyKey / Mikai).
 *
 * Questo header è il punto di ingresso per tutto il codice che manipola
 * i tag NFC SRIX4K usati dal sistema Mikai. Espone:
 *   - Le costanti fisiche del tag (numero di blocchi, dimensioni).
 *   - Le strutture che rappresentano il tag in memoria (srix_t, mykey_t).
 *   - Un flag-set bitmap per tracciare i blocchi modificati (srix_flag).
 *   - Le funzioni core mikai_* usate da MikaiMenuLogic e da Mikai.cpp.
 */

#pragma once

#include "pn532_srix.h"
#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// ─── Costanti fisiche del tag SRIX4K ─────────────────────────────────────────

#define SRIX4K_BLOCKS 128                                ///< Numero totale di blocchi EEPROM.
#define SRIX_BLOCK_LENGTH 4                              ///< Byte per blocco.
#define SRIX4K_BYTES (SRIX4K_BLOCKS * SRIX_BLOCK_LENGTH) ///< Dimensione totale EEPROM: 512 byte.

// ─── Bitmap dei blocchi modificati ───────────────────────────────────────────

/**
 * @brief Bitmap a 128 bit che traccia quali blocchi EEPROM sono stati modificati.
 *
 * Ogni bit corrisponde a un blocco (0..127). I 128 bit sono distribuiti su
 * 4 word da 32 bit per evitare dipendenze da tipi a 64 bit.
 */
struct srix_flag {
    uint32_t memory[4];
};

/** @brief Inizializza il flag-set azzerandolo (nessun blocco modificato). */
static inline struct srix_flag srix_flag_init() {
    struct srix_flag f;
    f.memory[0] = f.memory[1] = f.memory[2] = f.memory[3] = 0;
    return f;
}

/** @brief Marca il blocco @p b come modificato. */
static inline void srix_flag_add(struct srix_flag *f, uint8_t b) {
    if (b < 128) f->memory[b / 32] |= 1u << (b % 32);
}

/** @brief Rimuove il flag di modifica dal blocco @p b. */
static inline void srix_flag_remove(struct srix_flag *f, uint8_t b) {
    if (b < 128) f->memory[b / 32] &= ~(1u << (b % 32));
}

/** @brief Restituisce true se il blocco @p b è marcato come modificato. */
static inline bool srix_flag_get(struct srix_flag *f, uint8_t b) { return b < 128 && ((f->memory[b / 32] >> (b % 32)) & 1u); }

/** @brief Restituisce true se almeno un blocco risulta modificato. */
static inline bool srix_flag_isModified(struct srix_flag *f) { return (f->memory[0] | f->memory[1] | f->memory[2] | f->memory[3]) > 0; }

// ─── Strutture dati del tag ───────────────────────────────────────────────────

/**
 * @brief Immagine completa in RAM di un tag SRIX4K.
 *
 * Contiene la copia di tutti i 128 blocchi EEPROM, l'UID a 64 bit
 * e il flag-set che indica quali blocchi devono essere scritti sul tag.
 */
struct srix_t {
    uint8_t eeprom[SRIX4K_BLOCKS][SRIX_BLOCK_LENGTH]; ///< Copia dell'EEPROM (128 × 4 byte).
    uint64_t uid;                                     ///< UID del tag (little-endian).
    struct srix_flag srixFlag;                        ///< Blocchi modificati in attesa di scrittura.
};

/**
 * @brief Handle di lavoro che associa un tag (srix_t) alla sua chiave di cifratura.
 *
 * Tutte le funzioni mikai_* operano su questo handle per evitare
 * di passare UID e chiave separatamente.
 */
struct mykey_t {
    struct srix_t *srix4k;  ///< Puntatore all'immagine del tag in RAM.
    uint32_t encryptionKey; ///< Chiave di sessione calcolata da UID + blocchi vendor.
};

// ─── Stato globale condiviso ──────────────────────────────────────────────────

extern Arduino_PN532_SRIX nfc; ///< Istanza del driver PN532 (condivisa tra MikaiLogica e Mikai).
extern struct srix_t srix;     ///< Immagine RAM del tag attualmente letto.
extern struct mykey_t srixKey; ///< Handle di lavoro sul tag corrente.

// ─── API logica core (mikai_*) ────────────────────────────────────────────────
//
// Queste funzioni costituiscono l'unica interfaccia con cui il codice UI
// (MikaiMenuLogic) e il codice di classe (Mikai) interagiscono con il tag.
// Tutte operano su un handle mykey_t; il driver NFC viene passato esplicitamente
// solo alle funzioni che effettuano I/O fisico (read_tag, write_modified_blocks).

/** @brief Legge il tag NFC presente sul lettore e popola @p key. @return true se riuscito. */
bool mikai_read_tag(struct mykey_t *key, Arduino_PN532_SRIX *nfc);

/** @brief Compone una stringa leggibile con info, credito e storico transazioni. */
void mikai_get_info_string(struct mykey_t *key, char *out, size_t outLen);

/**
 * @brief Aggiunge @p cents centesimi al credito, registrando la transazione con la data indicata.
 * @return 0 ok, -1 Lock-ID attivo, -2 tag non associato, -3 importo < 5 centesimi.
 */
int mikai_add_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year);

/**
 * @brief Azzera lo storico e imposta il credito esatto a @p cents centesimi.
 * @return 0 ok, -1 errore interno.
 */
int mikai_set_cents(struct mykey_t *key, uint16_t cents, uint8_t day, uint8_t month, uint8_t year);

/**
 * @brief Scrive sul tag fisico tutti i blocchi marcati come modificati.
 * @return 0 sempre (gli errori di scrittura causano retry interni).
 */
int mikai_write_modified_blocks(struct mykey_t *key, Arduino_PN532_SRIX *nfc);

/** @brief Restituisce true se ci sono blocchi in attesa di scrittura. */
bool mikai_has_pending_writes(struct mykey_t *key);

/**
 * @brief Copia gli 8 byte raw dei blocchi vendor (0x18–0x19) in @p buffer.
 * @return 0 ok, -1 se il tag è in stato di reset.
 */
int mikai_export_vendor(struct mykey_t *key, uint8_t buffer[8]);

/** @brief Sostituisce i blocchi vendor, ricalcola la chiave di sessione e ri-cifra il credito. */
void mikai_import_vendor(struct mykey_t *key, const uint8_t block18[4], const uint8_t block19[4]);

/** @brief Restituisce il credito corrente in centesimi (valore decifrato). */
uint16_t mikai_get_current_credit(struct mykey_t *key);

/** @brief Porta il tag allo stato di reset di fabbrica (vendor non impostato). */
void mikai_reset_key(struct mykey_t *key);

/** @brief Restituisce true se il tag è in stato di reset (vendor non ancora impostato). */
bool mikai_is_reset(struct mykey_t *key);

/** @brief Restituisce true se il Lock-ID è attivo (tag in sola lettura). */
bool mikai_check_lock_id(struct mykey_t *key);

// ─── Funzioni ausiliarie ──────────────────────────────────────────────────────

/**
 * @brief Azzera i blocchi OTP (0x00–0x04) e ricalcola la chiave di sessione.
 *
 * Richiede un saldo sufficiente nel contatore OTP del blocco 0x06.
 */
void mikai_reset_otp(struct mykey_t *key);

/**
 * @brief Copia l'intera EEPROM e l'UID del tag nei buffer forniti.
 *
 * Il file dump ha formato: 8 byte UID (little-endian) + 512 byte EEPROM.
 */
void mikai_export_dump(struct mykey_t *key, uint64_t *uid_out, uint8_t eeprom_out[SRIX4K_BYTES]);

/**
 * @brief Sovrascrive un blocco arbitrario (0x10–0x7F) e lo marca come modificato.
 *
 * I blocchi di sistema (0x00–0x0F) vengono ignorati.
 */
void mikai_modify_block(struct mykey_t *key, const uint8_t block[4], uint8_t blockNum);
