#pragma once

/**
 * @file TessereLogica.h
 * @brief Strutture dati, costanti e API della logica core per tag MIFARE.
 *
 * Punto di ingresso per tutto il codice che manipola tag NFC MIFARE
 * (Classic 1K/4K/Mini e Ultralight) tramite il driver Adafruit_PN532.
 *
 * Percorsi SD usati dal modulo:
 *   - /rfid/tag/chiavi.txt          → chiavi di autenticazione (CSV uid,chiave)
 *   - /rfid/tag/gestori.txt         → lista nomi gestori (uno per riga)
 *   - /rfid/tag/gestori_mappa.txt   → associazioni UID→gestore (CSV uid,nome)
 *   - /rfid/tag/dump/<UIDHEX>.bin   → dump binari dei tag
 */

#include <Arduino.h>
#include <SD.h>
#include <array>
#include <vector>

// ─── Percorsi SD ─────────────────────────────────────────────────────────────

#define PERCORSO_RFID "/rfid"
#define PERCORSO_TAG "/rfid/tag"
#define PERCORSO_CHIAVI "/rfid/tag/chiavi.txt"
#define PERCORSO_GESTORI "/rfid/tag/gestori.txt"
#define PERCORSO_MAPPA "/rfid/tag/gestori_mappa.txt"
#define PERCORSO_DUMP_DIR "/rfid/tag/dump"

// ─── Costanti fisiche tag ─────────────────────────────────────────────────────

/// Blocchi massimi indirizzabili (MIFARE 4K ne ha 256)
#define MAX_BLOCCHI 256

/// Settori massimi (32 normali + 8 estesi per il 4K)
#define MAX_SETTORI 40

/// Pagine massime su MIFARE Ultralight / NTAG213
#define MAX_PAGINE_UL 45

// ─── Costanti file dump ───────────────────────────────────────────────────────

/// Firma magic dei primi 4 byte del file dump, usata per validazione
#define DUMP_FIRMA "MFDR"

/// Versione corrente del formato dump; versioni diverse vengono rifiutate
#define DUMP_VERSIONE 1

// ─── Struttura dump ───────────────────────────────────────────────────────────

/**
 * @brief Immagine completa in RAM di un tag MIFARE.
 *
 * Contiene identità del tag, dati EEPROM e chiavi per settore.
 * Dimensione in RAM: ~4.9 KB → dichiarare come `static` dove necessario
 * per evitare overflow dello stack ESP32 (default ~8 KB).
 */
struct DumpMifare {
    // Identità del tag
    uint8_t uid[7];       ///< UID del tag (4 o 7 byte; byte non usati = 0)
    uint8_t lunghezzaUid; ///< Lunghezza effettiva dell'UID in byte
    uint8_t sak;          ///< SAK (Select Acknowledge): identifica la famiglia MIFARE
    uint16_t atqa;        ///< ATQA (Answer To Request type A)
    String tipoTag;       ///< Descrizione leggibile del tipo (es. "Mifare 1K")
    uint8_t numSettori;   ///< Numero di settori del tag

    // Immagine EEPROM
    uint8_t dati[MAX_BLOCCHI][16]; ///< Contenuto dei blocchi (16 byte per blocco)
    bool bloccLetto[MAX_BLOCCHI];  ///< true se il blocco è stato letto con successo

    // Chiavi per settore
    uint8_t chiaveA[MAX_SETTORI][6];  ///< Key A trovata per ogni settore
    bool chiaveATrovata[MAX_SETTORI]; ///< true se la Key A del settore è nota
    uint8_t chiaveB[MAX_SETTORI][6];  ///< Key B trovata per ogni settore
    bool chiaveBTrovata[MAX_SETTORI]; ///< true se la Key B del settore è nota
};

// ─── Stato globale ────────────────────────────────────────────────────────────

/// Immagine RAM del tag attualmente in lavorazione (definita in TessereLogica.cpp)
extern DumpMifare dump_globale;

// ─── Helper UI ────────────────────────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo, poi attende la pressione di un tasto.
 * @param titolo Titolo del pannello (barra superiore Bruce).
 * @param corpo  Testo del corpo, righe separate da '\n'.
 */
void mostraMessaggio(const String &titolo, const String &corpo);

/**
 * @brief Mostra un pannello con titolo e corpo senza attendere input.
 *
 * Usata per messaggi di stato intermedi durante operazioni NFC lunghe.
 * Il programma prosegue immediatamente dopo il disegno.
 */
void mostraInfo(const String &titolo, const String &corpo);

// ─── Inizializzazione ─────────────────────────────────────────────────────────

/**
 * @brief Inizializza il driver PN532 via I²C (una sola volta per sessione).
 *
 * Legge i pin SDA/SCL dalla configurazione di Bruce, avvia il bus I²C a 100 kHz,
 * verifica la presenza del PN532 e configura la modalità SAM.
 * Se già inizializzato, ritorna true immediatamente.
 *
 * @return true se il PN532 risponde correttamente, false se non trovato.
 */
bool inizializzaNfc();

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

/**
 * @brief Attende un tag MIFARE ISO14443A sul lettore (timeout 6 secondi).
 *
 * In caso di successo popola dump_globale con: uid, lunghezzaUid, sak, atqa,
 * tipoTag, numSettori. Azzera anche i flag bloccLetto, chiaveATrovata, chiaveBTrovata.
 * I dati EEPROM NON vengono letti qui.
 *
 * @return true se un tag è stato rilevato entro il timeout.
 */
bool attesaTag();

/**
 * @brief Attende un tag MIFARE senza sovrascrivere dump_globale.
 *
 * Variante leggera di attesaTag(): esegue solo la lettura passiva.
 * Necessaria durante la scrittura per rilevare il tag target senza perdere
 * il dump sorgente già caricato in dump_globale.
 *
 * @param uid    Buffer di almeno 7 byte per l'UID rilevato.
 * @param lunghezza Lunghezza effettiva dell'UID (output).
 * @return true se un tag è stato rilevato entro 6 secondi.
 */
bool attesaTagQualsiasi(uint8_t *uid, uint8_t *lunghezza);

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

/**
 * @brief Restituisce la descrizione leggibile del tipo di tag dal byte SAK.
 *
 * Esempi: SAK 0x08 → "Mifare 1K", SAK 0x18 → "Mifare 4K", SAK 0x00 → "Mifare UL".
 */
String descrizioneTag(uint8_t sak);

/**
 * @brief Restituisce il numero di settori in base al byte SAK.
 *
 * MIFARE 4K → 40, Mini → 5, Classic 1K e altri → 16.
 */
uint8_t numSettoriDaSak(uint8_t sak);

/**
 * @brief Restituisce l'indice assoluto del blocco trailer del settore.
 *
 * Settori 0–31: trailer = sector × 4 + 3.
 * Settori 32–39 (4K): trailer nella zona a 16 blocchi.
 */
uint8_t bloccoTrailer(uint8_t settore);

/**
 * @brief Restituisce l'indice assoluto del primo blocco dati del settore.
 */
uint8_t primoBlocco(uint8_t settore);

/**
 * @brief Restituisce il numero di blocchi nel settore (4 normali, 16 per 4K esteso).
 */
uint8_t blocchiNelSettore(uint8_t settore);

// ─── Gestione chiavi ─────────────────────────────────────────────────────────

/**
 * @brief Carica le chiavi da PERCORSO_CHIAVI filtrate per l'UID specificato.
 *
 * Formato supportato (una voce per riga):
 *   - `AABBCCDD,FFEEDDCCBBAA` → chiave specifica per quel tag
 *   - `FFEEDDCCBBAA`          → chiave generica, valida per qualsiasi tag
 *
 * Le chiavi FF×6 e 00×6 sono sempre incluse come prime voci.
 *
 * @param uid    UID del tag corrente.
 * @param lunghezza Lunghezza dell'UID in byte.
 * @return Vettore di chiavi (6 byte ciascuna) da provare per l'autenticazione.
 */
std::vector<std::array<uint8_t, 6>> caricaChiavi(const uint8_t *uid, uint8_t lunghezza);

// ─── Gestione gestori ─────────────────────────────────────────────────────────

/**
 * @brief Carica la lista dei nomi gestore da PERCORSO_GESTORI.
 * @return Vettore di stringhe con i nomi, vuoto se il file non esiste.
 */
std::vector<String> caricaGestori();

/**
 * @brief Aggiunge un nuovo nome gestore in PERCORSO_GESTORI.
 *
 * Non aggiunge duplicati. Crea le cartelle se non esistono.
 *
 * @return true se aggiunto, false se già esistente o errore SD.
 */
bool aggiungiGestore(const String &nome);

/**
 * @brief Elimina un gestore da gestori.txt e tutte le sue associazioni da gestori_mappa.txt.
 *
 * Riscrive entrambi i file tramite file temporanei per garantire integrità.
 *
 * @return true se trovato ed eliminato.
 */
bool eliminaGestore(const String &nome);

/**
 * @brief Rinomina un gestore in gestori.txt e aggiorna tutte le voci in gestori_mappa.txt.
 *
 * @return true se il gestore è stato trovato e rinominato.
 */
bool modificaGestore(const String &vecchioNome, const String &nuovoNome);

/**
 * @brief Associa un UID a un gestore in PERCORSO_MAPPA.
 *
 * Se l'UID era già associato a un altro gestore, sovrascrive l'associazione.
 *
 * @param uidHex UID in formato esadecimale maiuscolo (es. "AABBCCDD").
 * @param nome   Nome del gestore da associare.
 * @return true se l'associazione è stata salvata correttamente.
 */
bool associaGestore(const String &uidHex, const String &nome);

/**
 * @brief Cerca il nome del gestore associato all'UID in PERCORSO_MAPPA.
 *
 * @param uidHex UID in formato esadecimale maiuscolo.
 * @return Nome del gestore se trovato, stringa vuota se non presente.
 */
String cercaGestore(const String &uidHex);

// ─── Operazioni core ─────────────────────────────────────────────────────────

/**
 * @brief Legge tutti i settori leggibili del tag in dump_globale.
 *
 * Prova le chiavi da SD (prima Key A, poi Key B) e ripristina le chiavi
 * nel trailer dopo la lettura (l'hardware le oscura come 00×6).
 *
 * @param settoriLetti Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
bool leggiDump(uint8_t &settoriLetti);

/**
 * @brief Legge tutti i settori usando le chiavi già presenti in dump_globale.
 *
 * Non carica chiavi da SD: usa direttamente dump_globale.chiaveA e chiaveB.
 * Usata dopo microelInjectKeys() quando le chiavi KDF sono già in RAM.
 *
 * @param settoriLetti Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
bool leggiDumpConChiavi(uint8_t &settoriLetti);

/**
 * @brief Scrive un dump sul tag fisico presente sul lettore (blocco 0 escluso).
 *
 * Autentica ogni settore con le chiavi del dump (Key A prima, poi Key B).
 * Salta il blocco 0 (read-only su tag originali) e i blocchi non letti.
 *
 * @warning Scrivere un sector trailer errato può rendere il settore inaccessibile
 *          in modo permanente. Verificare sempre le chiavi prima di procedere.
 *
 * @param sorgente       Dump sorgente da scrivere sul tag.
 * @param settoriScritti Numero di settori scritti con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
bool scriviDump(const DumpMifare &sorgente, uint8_t &settoriScritti);

/**
 * @brief Scrive il blocco 0 (UID, BCC, dati produttore) su tag magic.
 *
 * Funziona solo su tag magic (CUID, GEN2, ecc.) che permettono la sovrascrittura
 * del blocco 0. Su tag originali la scrittura viene ignorata dall'hardware.
 *
 * @warning Un UID o BCC errati possono rendere il tag irrilevabile dai lettori.
 *
 * @param sorgente Dump contenente il blocco 0 da scrivere.
 * @return true se la scrittura è riuscita.
 */
bool scriviBloc0(const DumpMifare &sorgente);

// ─── Persistenza su SD ───────────────────────────────────────────────────────

/**
 * @brief Salva il dump in PERCORSO_DUMP_DIR/<uidHex>.bin sulla SD.
 *
 * Formato binario (~4.9 KB):
 *   4 byte  → firma "MFDR"
 *   1 byte  → versione
 *   1 byte  → lunghezzaUid
 *   7 byte  → uid
 *   1 byte  → sak
 *   2 byte  → atqa
 *   1 byte  → numSettori
 *   240 byte → chiaveA[40][6]
 *   40 byte  → chiaveATrovata[40]
 *   240 byte → chiaveB[40][6]
 *   40 byte  → chiaveBTrovata[40]
 *   256 byte → bloccLetto[256]
 *   4096 byte → dati[256][16]
 *
 * @param dump   Dump da salvare.
 * @param uidHex Stringa esadecimale dell'UID, usata come nome del file.
 * @return true se la scrittura è riuscita.
 */
bool salvaDump(const DumpMifare &dump, const String &uidHex);

/**
 * @brief Carica un dump da percorso assoluto nella struct @p dump.
 *
 * Verifica firma e versione prima di caricare. Ripristina tipoTag e numSettori
 * dal SAK.
 *
 * @param dump     Struttura di destinazione.
 * @param percorso Percorso assoluto del file (es. "/rfid/tag/dump/AABBCCDD.bin").
 * @return true se il file è valido e i dati sono stati caricati.
 */
bool caricaDump(DumpMifare &dump, const String &percorso);

/**
 * @brief Costruisce la stringa esadecimale maiuscola dell'UID.
 *
 * Esempio: {0xAA, 0xBB, 0xCC, 0xDD} → "AABBCCDD".
 *
 * @param uid Puntatore all'array dei byte dell'UID.
 * @param lunghezza Numero di byte da convertire.
 * @return Stringa esadecimale maiuscola senza separatori.
 */
String uidInHex(const uint8_t *uid, uint8_t lunghezza);
