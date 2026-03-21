/**
 * @file TessereLogic.h
 * @brief Strutture dati, costanti e API della logica core per tag MIFARE.
 *
 * Questo header è il punto di ingresso per tutto il codice che manipola
 * tag NFC MIFARE (Classic 1K/4K/Mini e Ultralight) tramite il driver
 * Adafruit_PN532. Espone:
 *   - Le costanti fisiche (max blocchi, settori, magic del file dump)
 *   - La struttura MifareDump che rappresenta il tag in memoria
 *   - Le funzioni core usate da TessereMenuLogic e da Tessere.cpp
 *
 * Percorsi SD usati dal modulo:
 *   - /rfid/chiavi.txt       → chiavi di autenticazione (CSV uid,chiave)
 *   - /rfid/gestori.txt      → lista nomi gestori (uno per riga)
 *   - /rfid/gestori_map.txt  → associazioni UID→gestore (CSV uid,nome)
 *   - /rfid/dumps/           → dump binari dei tag (<UIDHEX>.bin)
 */

#pragma once

#include <Arduino.h>
#include <SD.h>
#include <array>
#include <vector>

// ─── Costanti fisiche ─────────────────────────────────────────────────────────

#define MIFARE_MAX_BLOCKS 256  ///< Blocchi massimi indirizzabili (MIFARE 4K)
#define MIFARE_MAX_SECTORS 40  ///< Settori massimi (32×4-block + 8×16-block per 4K)
#define MIFARE_UL_MAX_PAGES 45 ///< Pagine massime lette su MIFARE Ultralight / NTAG213

// ─── Costanti file dump ───────────────────────────────────────────────────────

#define DUMP_MAGIC "MFDR" ///< Firma identificativa del file dump (4 byte)
#define DUMP_VERSION 1    ///< Versione corrente del formato dump

// ─── Struttura dump ───────────────────────────────────────────────────────────

/**
 * @brief Immagine completa in RAM di un tag MIFARE.
 *
 * Raccoglie tutto ciò che serve per identificare, leggere, salvare e
 * riscrivere un tag: identità (UID, SAK, ATQA), copia dell'EEPROM e
 * le chiavi Key A / Key B trovate per ogni settore.
 */
struct MifareDump {
    // ── Identità del tag ─────────────────────────────────────────────────────
    uint8_t uid[7];     ///< UID del tag (4 o 7 byte, i rimanenti sono 0)
    uint8_t uidLen;     ///< Lunghezza effettiva dell'UID (4 o 7)
    uint8_t sak;        ///< SAK (Select Acknowledge) – identifica il tipo
    uint16_t atqa;      ///< ATQA (Answer To Request type A)
    String tagType;     ///< Descrizione leggibile del tipo (es. "Mifare 1K")
    uint8_t numSectors; ///< Numero di settori del tag

    // ── Immagine EEPROM ───────────────────────────────────────────────────────
    uint8_t data[MIFARE_MAX_BLOCKS][16]; ///< Dati dei blocchi (16 byte per blocco)
    bool blockRead[MIFARE_MAX_BLOCKS];   ///< true se il blocco è stato letto con successo

    // ── Chiavi per settore ────────────────────────────────────────────────────
    uint8_t keyA[MIFARE_MAX_SECTORS][6]; ///< Key A trovata per ogni settore
    bool keyAFound[MIFARE_MAX_SECTORS];  ///< true se Key A è stata trovata
    uint8_t keyB[MIFARE_MAX_SECTORS][6]; ///< Key B trovata per ogni settore
    bool keyBFound[MIFARE_MAX_SECTORS];  ///< true se Key B è stata trovata
};

// ─── Stato globale condiviso ──────────────────────────────────────────────────

/// Immagine RAM del tag attualmente letto/in lavorazione.
extern MifareDump g_dump;

// ─── Inizializzazione ─────────────────────────────────────────────────────────

/**
 * @brief Inizializza il driver PN532 via I²C (lazy, una sola volta per sessione).
 * @return true se il PN532 risponde correttamente.
 */
bool mifareInit();

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

/**
 * @brief Attende un tag MIFARE sul lettore (timeout 6 s) e popola g_dump.
 *
 * Dopo una chiamata riuscita, g_dump contiene: uid, uidLen, sak, atqa,
 * tagType e numSectors. I dati EEPROM NON vengono letti qui.
 * @return true se un tag è stato rilevato e selezionato.
 */
bool waitForMifareTag();

/**
 * @brief Attende un tag MIFARE sul lettore senza popolare g_dump.
 *
 * Variante leggera di waitForMifareTag(): esegue solo la lettura passiva
 * e restituisce UID e lunghezza. Usata da WriteTessera() per rilevare
 * il tag target prima di scrivere, senza sovrascrivere g_dump che
 * contiene il dump sorgente già caricato dalla SD.
 *
 * @param uid    Buffer di destinazione per l'UID (minimo 7 byte).
 * @param uidLen Lunghezza effettiva dell'UID rilevato (output).
 * @return true se un tag è stato rilevato entro il timeout di 6 secondi.
 */
bool waitForAnyMifareTag(uint8_t *uid, uint8_t *uidLen);

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

/** @brief Restituisce una stringa leggibile per il tipo di tag dal byte SAK. */
String getTagType(uint8_t sak);

/** @brief Restituisce il numero di settori per il tipo di tag dal byte SAK. */
uint8_t getSectorCount(uint8_t sak);

/** @brief Restituisce l'indice del blocco trailer (sector trailer) del settore @p sector. */
uint8_t trailerBlock(uint8_t sector);

/** @brief Restituisce l'indice del primo blocco dati del settore @p sector. */
uint8_t firstBlock(uint8_t sector);

/** @brief Restituisce il numero di blocchi nel settore @p sector (4 o 16). */
uint8_t blocksInSector(uint8_t sector);

// ─── Gestione chiavi ─────────────────────────────────────────────────────────

/**
 * @brief Carica le chiavi da /rfid/chiavi.txt filtrate per l'UID specificato.
 *
 * Formato CSV del file (una voce per riga):
 *   - `uid,chiave`  → chiave specifica per quel tag (es. `AABBCCDD,FFEEDDCCBBAA`)
 *   - `chiave`      → chiave generica, valida per qualsiasi tag
 *
 * Le chiavi `FF FF FF FF FF FF` e `00 00 00 00 00 00` vengono sempre aggiunte.
 *
 * @param uid    UID del tag corrente (usato per filtrare le chiavi specifiche).
 * @param uidLen Lunghezza dell'UID.
 * @return Vettore di chiavi (6 byte ciascuna) da provare per l'autenticazione.
 */
std::vector<std::array<uint8_t, 6>> loadKeysForUID(const uint8_t *uid, uint8_t uidLen);

// ─── Gestione gestori ─────────────────────────────────────────────────────────
//
// I gestori sono memorizzati su due file separati:
//   - gestori.txt     → lista dei nomi (uno per riga), modificata dal menu Gestori
//   - gestori_map.txt → associazioni uid,nome, aggiornate dalla Read e dal menu Gestori

/**
 * @brief Carica la lista dei nomi gestore da /rfid/gestori.txt.
 *
 * Il file contiene un nome per riga (es. "Sto&Bene").
 * Righe vuote vengono ignorate.
 *
 * @return Vettore di stringhe con i nomi dei gestori disponibili.
 */
std::vector<String> loadGestori();

/**
 * @brief Aggiunge un nuovo nome gestore in /rfid/gestori.txt.
 *
 * Non aggiunge duplicati: se il nome esiste già, non fa nulla.
 * Crea il file e la cartella /rfid se non esistono.
 *
 * @param nome Nome del gestore da aggiungere.
 * @return true se il gestore è stato aggiunto, false se già esistente o errore SD.
 */
bool addGestore(const String &nome);

/**
 * @brief Elimina un gestore da /rfid/gestori.txt e tutte le sue associazioni.
 *
 * Riscrive gestori.txt escludendo il nome indicato.
 * Rimuove anche tutte le voci corrispondenti in gestori_map.txt.
 *
 * @param nome Nome del gestore da eliminare.
 * @return true se il gestore è stato trovato ed eliminato.
 */
bool deleteGestore(const String &nome);

/**
 * @brief Rinomina un gestore in gestori.txt e aggiorna gestori_map.txt.
 *
 * Aggiorna sia la lista nomi che tutte le associazioni UID che
 * usavano il vecchio nome, mantenendo la coerenza tra i due file.
 *
 * @param oldNome Nome attuale del gestore.
 * @param newNome Nuovo nome da assegnare.
 * @return true se il gestore è stato trovato e rinominato.
 */
bool modifyGestore(const String &oldNome, const String &newNome);

/**
 * @brief Associa un UID a un gestore in /rfid/gestori_map.txt.
 *
 * Se l'UID era già associato a un altro gestore, sovrascrive l'associazione.
 * Crea il file se non esiste.
 * Formato CSV: uid,nome (es. 1E733840,Sto&Bene).
 *
 * @param uidHex UID del tag in formato esadecimale maiuscolo.
 * @param nome   Nome del gestore da associare.
 * @return true se l'associazione è stata salvata correttamente.
 */
bool associateGestore(const String &uidHex, const String &nome);

/**
 * @brief Cerca il nome del gestore associato all'UID in /rfid/gestori_map.txt.
 *
 * Legge il file CSV riga per riga confrontando l'UID (case-insensitive).
 * Righe malformate o senza separatore ',' vengono ignorate.
 *
 * @param uidHex UID del tag in formato esadecimale maiuscolo (es. "1E733840").
 * @return Nome del gestore se trovato, stringa vuota se il file non esiste
 *         o se l'UID non è presente.
 */
String lookupGestore(const String &uidHex);

// ─── Operazioni core ─────────────────────────────────────────────────────────

/**
 * @brief Legge tutti i settori leggibili del tag in g_dump.
 *
 * Per ogni settore prova le chiavi caricate da SD (prima Key A, poi Key B).
 * Gestisce separatamente MIFARE Classic e MIFARE Ultralight.
 *
 * @param sectorsRead Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
bool mifareReadDump(uint8_t &sectorsRead);

/**
 * @brief Scrive un dump sul tag fisico presente sul lettore.
 *
 * Autentica ogni settore con le chiavi memorizzate nel dump.
 * Salta il blocco 0 (dati produttore, read-only) e i blocchi non letti.
 * @warning Scrivere un sector trailer errato può rendere il tag inutilizzabile.
 *
 * @param src            Dump da scrivere.
 * @param sectorsWritten Numero di settori scritti con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
bool mifareWriteDump(const MifareDump &src, uint8_t &sectorsWritten);

// ─── Persistenza su SD ───────────────────────────────────────────────────────

/**
 * @brief Salva il dump in /rfid/dumps/<uidHex>.bin sulla SD.
 *
 * Formato del file: header binario (magic, versione, UID, SAK, ATQA,
 * chiavi per settore, flag blockRead) seguito dai 256×16 byte di dati.
 *
 * @param dump   Dump da salvare.
 * @param uidHex Stringa esadecimale dell'UID (usata come nome file).
 * @return true se la scrittura è riuscita.
 */
bool saveDumpToSD(const MifareDump &dump, const String &uidHex);

/**
 * @brief Carica un dump da un percorso assoluto sulla SD in @p dump.
 * @param dump Struttura di destinazione.
 * @param path Percorso assoluto del file (es. "/rfid/dumps/AABBCCDD.bin").
 * @return true se il file è stato caricato correttamente.
 */
bool loadDumpFromSD(MifareDump &dump, const String &path);

/**
 * @brief Costruisce la stringa esadecimale maiuscola dell'UID.
 * @param uid Puntatore all'array UID.
 * @param len Lunghezza dell'UID in byte.
 * @return Stringa esadecimale (es. "AABBCCDD").
 */
String buildUIDHex(const uint8_t *uid, uint8_t len);

// ─── Azioni menu (dichiarate qui, implementate in TessereMenuLogic.cpp) ───────

/** @brief Mostra UID, SAK, ATQA, tipo e gestore del tag a schermo. */
void InfoTessera();

/** @brief Legge il dump del tag, lo salva su SD e chiede di associare un gestore. */
void ReadTessera();

/** @brief Scrive sul tag un dump selezionato dalla SD. */
void WriteTessera();

/** @brief Scrive automaticamente il dump corretto in base all'UID del tag. */
void AutoWriteTessera();

/** @brief Menu di gestione gestori: aggiungi, modifica, elimina. */
void GestoriMenu();
