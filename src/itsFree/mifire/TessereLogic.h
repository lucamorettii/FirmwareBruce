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
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunta dichiarazione di mifareWriteBlock0() usata dal modulo Microel
 *     per scrivere il blocco 0 (dati produttore) su tag magic/cinesi.
 */

#pragma once

#include <Arduino.h>
#include <SD.h>
#include <array>
#include <vector>

// ─── Costanti fisiche ─────────────────────────────────────────────────────────

/// Blocchi massimi indirizzabili: MIFARE 4K ne ha 256 (32 settori × 4 blocchi + 8 × 16 blocchi)
#define MIFARE_MAX_BLOCKS 256

/// Settori massimi: 32 settori da 4 blocchi + 8 settori da 16 blocchi (solo 4K)
#define MIFARE_MAX_SECTORS 40

/// Pagine massime lette su MIFARE Ultralight / NTAG213 (4 byte per pagina)
#define MIFARE_UL_MAX_PAGES 45

// ─── Costanti file dump ───────────────────────────────────────────────────────

/// Firma magic dei 4 byte iniziali del file dump, usata per validare il file in loadDumpFromSD()
#define DUMP_MAGIC "MFDR"

/// Versione corrente del formato dump; se non corrisponde, loadDumpFromSD() rifiuta il file
#define DUMP_VERSION 1

// ─── Struttura dump ───────────────────────────────────────────────────────────

/**
 * @brief Immagine completa in RAM di un tag MIFARE.
 *
 * Raccoglie tutto ciò che serve per identificare, leggere, salvare e
 * riscrivere un tag NFC:
 *   - Identità: UID, SAK, ATQA, tipo stringa, numero settori
 *   - EEPROM:   256 blocchi × 16 byte + flag di lettura per blocco
 *   - Chiavi:   Key A e Key B trovate per ogni settore + flag di validità
 *
 * Dimensione in RAM: circa 4.9 KB → dichiarare come static dove necessario
 * per evitare overflow dello stack ESP32 (stack default ~8 KB).
 */
struct MifareDump {
    // ── Identità del tag ─────────────────────────────────────────────────────

    uint8_t uid[7];     ///< UID del tag (4 o 7 byte; i byte non usati sono 0)
    uint8_t uidLen;     ///< Lunghezza effettiva dell'UID in byte (4 o 7)
    uint8_t sak;        ///< SAK (Select Acknowledge): identifica la famiglia MIFARE
    uint16_t atqa;      ///< ATQA (Answer To Request type A): risposta all'anti-collision
    String tagType;     ///< Descrizione leggibile del tipo (es. "Mifare 1K", "Mifare UL")
    uint8_t numSectors; ///< Numero di settori del tag (5 Mini / 16 Classic 1K / 40 Classic 4K)

    // ── Immagine EEPROM ───────────────────────────────────────────────────────

    uint8_t data[MIFARE_MAX_BLOCKS][16]; ///< Contenuto dei blocchi (16 byte per blocco)
    bool blockRead[MIFARE_MAX_BLOCKS];   ///< true se il blocco è stato letto con successo

    // ── Chiavi per settore ────────────────────────────────────────────────────

    uint8_t keyA[MIFARE_MAX_SECTORS][6]; ///< Key A trovata per ogni settore (6 byte)
    bool keyAFound[MIFARE_MAX_SECTORS];  ///< true se la Key A del settore è nota
    uint8_t keyB[MIFARE_MAX_SECTORS][6]; ///< Key B trovata per ogni settore (6 byte)
    bool keyBFound[MIFARE_MAX_SECTORS];  ///< true se la Key B del settore è nota
};

// ─── Stato globale condiviso ──────────────────────────────────────────────────

/// Immagine RAM del tag attualmente letto o in lavorazione (definita in TessereLogic.cpp).
extern MifareDump g_dump;

// ─── Inizializzazione ─────────────────────────────────────────────────────────

/**
 * @brief Inizializza il driver PN532 via I²C (lazy: lo fa una sola volta per sessione).
 *
 * Legge i pin SDA/SCL dalla configurazione di Bruce, avvia il bus I²C a 100 kHz,
 * verifica la presenza del PN532 e configura la modalità SAM per la lettura passiva.
 * Se già inizializzato, ritorna true immediatamente senza fare nulla.
 *
 * @return true se il PN532 risponde correttamente, false se non trovato.
 */
bool mifareInit();

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

/**
 * @brief Attende un tag MIFARE ISO14443A sul lettore (timeout 6 secondi).
 *
 * In caso di successo popola g_dump con: uid, uidLen, sak, atqa, tagType,
 * numSectors. Azzera anche i flag blockRead, keyAFound, keyBFound.
 * I dati EEPROM NON vengono letti qui: serve una chiamata a mifareReadDump().
 *
 * @return true se un tag è stato rilevato entro il timeout.
 */
bool waitForMifareTag();

/**
 * @brief Attende un tag MIFARE sul lettore senza sovrascrivere g_dump.
 *
 * Variante leggera di waitForMifareTag(): esegue solo la lettura passiva
 * e restituisce UID e lunghezza nei parametri di output.
 * Necessaria in WriteTessera() per rilevare il tag target su cui scrivere
 * senza perdere il dump sorgente già caricato in g_dump dalla SD.
 *
 * @param uid    Buffer di destinazione per l'UID (almeno 7 byte).
 * @param uidLen Lunghezza effettiva dell'UID rilevato (output).
 * @return true se un tag è stato rilevato entro 6 secondi.
 */
bool waitForAnyMifareTag(uint8_t *uid, uint8_t *uidLen);

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

/**
 * @brief Restituisce una stringa leggibile per il tipo di tag dal byte SAK.
 *
 * Esempi: SAK 0x08 → "Mifare 1K", SAK 0x18 → "Mifare 4K", SAK 0x00 → "Mifare UL".
 */
String getTagType(uint8_t sak);

/**
 * @brief Restituisce il numero di settori in base al byte SAK.
 *
 * MIFARE 4K (SAK 0x18) → 40 settori.
 * MIFARE Mini (SAK 0x09) → 5 settori.
 * Tutti gli altri → 16 settori (Classic 1K default).
 */
uint8_t getSectorCount(uint8_t sak);

/**
 * @brief Restituisce l'indice assoluto del blocco trailer del settore @p sector.
 *
 * Settori 0–31: trailer = sector × 4 + 3.
 * Settori 32–39 (4K esteso): trailer = 128 + (sector − 32) × 16 + 15.
 */
uint8_t trailerBlock(uint8_t sector);

/**
 * @brief Restituisce l'indice assoluto del primo blocco dati del settore @p sector.
 *
 * Settori 0–31: primo blocco = sector × 4.
 * Settori 32–39 (4K esteso): primo blocco nella zona a 16 blocchi.
 */
uint8_t firstBlock(uint8_t sector);

/**
 * @brief Restituisce il numero di blocchi nel settore @p sector.
 *
 * Settori 0–31: 4 blocchi. Settori 32–39 (4K): 16 blocchi.
 */
uint8_t blocksInSector(uint8_t sector);

// ─── Gestione chiavi ─────────────────────────────────────────────────────────

/**
 * @brief Carica le chiavi da /rfid/chiavi.txt filtrate per l'UID specificato.
 *
 * Formato CSV del file (una voce per riga):
 *   - `uid,chiave`  → chiave specifica per quel tag (es. `AABBCCDD,FFEEDDCCBBAA`)
 *   - `chiave`      → chiave generica, valida per qualsiasi tag
 *
 * Le chiavi FF×6 e 00×6 vengono sempre incluse come prime voci da provare.
 * Righe malformate o con caratteri non esadecimali vengono ignorate silenziosamente.
 *
 * @param uid    UID del tag corrente (filtra le chiavi specifiche per quel tag).
 * @param uidLen Lunghezza dell'UID in byte.
 * @return Vettore di chiavi (6 byte ciascuna) da provare per l'autenticazione.
 */
std::vector<std::array<uint8_t, 6>> loadKeysForUID(const uint8_t *uid, uint8_t uidLen);

// ─── Gestione gestori ─────────────────────────────────────────────────────────

/**
 * @brief Carica la lista dei nomi gestore da /rfid/gestori.txt.
 *
 * Il file contiene un nome per riga (es. "Sto&Bene"). Righe vuote ignorate.
 * @return Vettore di stringhe con i nomi dei gestori disponibili.
 */
std::vector<String> loadGestori();

/**
 * @brief Aggiunge un nuovo nome gestore in /rfid/gestori.txt.
 *
 * Non aggiunge duplicati: se il nome esiste già, ritorna false.
 * Crea la cartella /rfid e il file se non esistono.
 *
 * @param nome Nome del gestore da aggiungere.
 * @return true se il gestore è stato aggiunto, false se già presente o errore SD.
 */
bool addGestore(const String &nome);

/**
 * @brief Elimina un gestore da gestori.txt e tutte le sue associazioni da gestori_map.txt.
 *
 * Riscrive entrambi i file tramite file temporanei per garantire integrità
 * in caso di interruzione durante la scrittura.
 *
 * @param nome Nome del gestore da eliminare.
 * @return true se il gestore è stato trovato ed eliminato.
 */
bool deleteGestore(const String &nome);

/**
 * @brief Rinomina un gestore in gestori.txt e aggiorna tutte le voci in gestori_map.txt.
 *
 * Mantiene la coerenza tra i due file: tutte le associazioni UID che usavano
 * il vecchio nome vengono aggiornate al nuovo nome automaticamente.
 *
 * @param oldNome Nome attuale del gestore da rinominare.
 * @param newNome Nuovo nome da assegnare.
 * @return true se il gestore è stato trovato e rinominato.
 */
bool modifyGestore(const String &oldNome, const String &newNome);

/**
 * @brief Associa un UID a un gestore in /rfid/gestori_map.txt.
 *
 * Se l'UID era già associato a un altro gestore, sovrascrive l'associazione.
 * Formato CSV del file: uid,nome (es. 1E733840,Sto&Bene).
 *
 * @param uidHex UID del tag in formato esadecimale maiuscolo (es. "AABBCCDD").
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
 * @return Nome del gestore se trovato, stringa vuota se non presente.
 */
String lookupGestore(const String &uidHex);

// ─── Operazioni core ─────────────────────────────────────────────────────────

/**
 * @brief Legge tutti i settori leggibili del tag in g_dump.
 *
 * Per ogni settore prova le chiavi caricate da SD (prima Key A, poi Key B).
 * Dopo la lettura del trailer, ripristina Key A (l'hardware la oscura come 00×6).
 * Gestisce separatamente MIFARE Classic (con autenticazione) e Ultralight (senza).
 *
 * @param sectorsRead Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
bool mifareReadDump(uint8_t &sectorsRead);

/**
 * @brief Scrive un dump sul tag fisico presente sul lettore.
 *
 * Autentica ogni settore con le chiavi memorizzate nel dump (Key A prima, Key B come fallback).
 * Salta il blocco 0 (dati produttore, read-only su tag originali) e i blocchi non letti.
 *
 * @warning Scrivere un sector trailer errato può rendere il settore inaccessibile
 *          in modo permanente. Verificare sempre le chiavi prima di procedere.
 *
 * @param src            Dump sorgente da scrivere sul tag.
 * @param sectorsWritten Numero di settori scritti con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
bool mifareWriteDump(const MifareDump &src, uint8_t &sectorsWritten);

/**
 * @brief Scrive il blocco 0 (dati produttore / UID) del dump sul tag fisico.
 *
 * Il blocco 0 contiene: UID (4 byte), BCC (byte di controllo), SAK, ATQA e
 * dati del produttore. Sui tag originali MIFARE questo blocco è read-only e
 * la scrittura verrà ignorata dall'hardware.
 *
 * Questa funzione è destinata esclusivamente ai tag magic (CUID, FUID, GEN2, ecc.)
 * che permettono la sovrascrittura del blocco 0, consentendo la clonazione completa.
 *
 * Autentica il settore 0 con Key A (o Key B come fallback) prima di scrivere.
 *
 * @warning Scrivere un blocco 0 con UID o BCC errati su tag magic può rendere
 *          il tag non rilevabile dai lettori. Usare con consapevolezza.
 *
 * @param src Dump sorgente da cui leggere il contenuto del blocco 0.
 * @return true se la scrittura è riuscita, false se il blocco 0 non è nel dump,
 *         se l'autenticazione fallisce, o se il tag non è magic.
 */
bool mifareWriteBlock0(const MifareDump &src);

// ─── Persistenza su SD ───────────────────────────────────────────────────────

/**
 * @brief Salva il dump in /rfid/dumps/<uidHex>.bin sulla SD.
 *
 * Formato del file binario (little-endian, circa 4.9 KB):
 *   4 byte  → magic "MFDR"
 *   1 byte  → versione formato
 *   1 byte  → uidLen
 *   7 byte  → uid (padding con zeri)
 *   1 byte  → sak
 *   2 byte  → atqa
 *   1 byte  → numSectors
 *   240 byte → keyA[40][6]
 *   40 byte  → keyAFound[40]
 *   240 byte → keyB[40][6]
 *   40 byte  → keyBFound[40]
 *   256 byte → blockRead[256]
 *   4096 byte → data[256][16]
 *
 * @param dump   Dump da serializzare e salvare.
 * @param uidHex Stringa esadecimale dell'UID, usata come nome del file.
 * @return true se la scrittura è riuscita.
 */
bool saveDumpToSD(const MifareDump &dump, const String &uidHex);

/**
 * @brief Carica un dump da un percorso assoluto sulla SD nella struct @p dump.
 *
 * Verifica la firma magic e la versione prima di caricare i dati.
 * Ripristina automaticamente tagType e numSectors dal SAK salvato.
 *
 * @param dump Struttura di destinazione dove caricare i dati.
 * @param path Percorso assoluto del file (es. "/rfid/dumps/AABBCCDD.bin").
 * @return true se il file è valido e i dati sono stati caricati correttamente.
 */
bool loadDumpFromSD(MifareDump &dump, const String &path);

/**
 * @brief Costruisce la stringa esadecimale maiuscola dell'UID.
 *
 * Usata per generare nomi di file dump e per le ricerche in gestori_map.txt.
 * Esempio: UID {0xAA, 0xBB, 0xCC, 0xDD} → "AABBCCDD".
 *
 * @param uid Puntatore all'array dei byte dell'UID.
 * @param len Numero di byte dell'UID da convertire.
 * @return Stringa esadecimale maiuscola senza separatori.
 */
String buildUIDHex(const uint8_t *uid, uint8_t len);

// ─── Azioni menu (dichiarate qui, implementate in TessereMenuLogic.cpp) ───────

void InfoTessera();      ///< Mostra UID, SAK, ATQA, tipo e gestore a schermo.
void ReadTessera();      ///< Legge il dump, lo salva su SD, chiede associazione gestore.
void WriteTessera();     ///< Scrive sul tag un dump selezionato dalla SD.
void AutoWriteTessera(); ///< Scrive automaticamente il dump in base all'UID del tag.
void GestoriMenu();      ///< Menu gestione gestori: aggiungi, modifica, elimina.
