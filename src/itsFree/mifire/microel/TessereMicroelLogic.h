/**
 * @file TessereMicroelLogic.h
 * @brief Logica KDF e gestione credito per tessere MIFARE Microel.
 *
 * Questo modulo estende TessereLogic per supportare le tessere Microel,
 * un sistema di pagamento prepagato su MIFARE Classic 1K usato in Italia
 * su distributori automatici e sistemi di accesso.
 *
 * La logica implementata comprende:
 *
 *  1. KDF (Key Derivation Function)
 *     Le tessere Microel non usano chiavi fisse: Key A e Key B vengono
 *     derivate matematicamente dall'UID del tag tramite un algoritmo a 3 step:
 *       UID → sumHex → Key A → Key B
 *     Questo significa che non è necessario conoscere le chiavi in anticipo:
 *     basta l'UID per autenticarsi su qualsiasi tessera Microel.
 *
 *  2. Parser del blocco dati (blocco 4)
 *     Il blocco 4 contiene le informazioni operative della tessera in
 *     formato little-endian su 16 byte: numero operazione, credito,
 *     data transazione, punti, checksum.
 *
 *  3. Menu ricarica
 *     Flusso guidato per impostare un nuovo credito: leggi → scegli
 *     importo → conferma → scrivi. Importi disponibili: 5/10/15/20/25 €.
 *
 *  4. Info card su display
 *     Mostra UID, Key A/B derivate, credito corrente/precedente e gestore.
 *
 *  5. Scrittura con blocco 0
 *     Scrive il dump completo incluso il blocco 0 (UID/produttore) per
 *     la clonazione su tag magic (CUID, GEN2, ecc.).
 *
 * Dipendenze: TessereLogic.h (MifareDump, g_dump, mifareWriteBlock0, ecc.)
 *
 * Struttura blocco 4 (16 byte, formato little-endian):
 *   Byte  0–1  → Numero operazione (uint16)
 *   Byte  2–3  → Somma totale input (uint16)
 *   Byte  4    → Deposito (uint8)
 *   Byte  5–6  → Credito corrente in centesimi (uint16) ← campo modificato
 *   Byte  7–10 → Data ultima transazione (uint32)
 *   Byte 11–12 → Punti accumulati (uint16)
 *   Byte 13–14 → Importo ultima transazione in centesimi (uint16)
 *   Byte 15    → Checksum (somma byte 0–14 mod 256)
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunta microelInfoCard(): mostra su display UID, Key A/B, credito, gestore.
 *   - Aggiunta microelWriteCard(): scrive dump + blocco 0 (per tag magic).
 */

#pragma once

#include "itsFree/mifire/TessereLogic.h" // MifareDump, g_dump, mifareWriteBlock0(), buildUIDHex()...
#include <Arduino.h>

// ─── Costanti KDF ─────────────────────────────────────────────────────────────

#define MICROEL_KEY_LENGTH 6 ///< Lunghezza in byte di Key A e Key B (standard MIFARE)
#define MICROEL_UID_LENGTH 4 ///< Lunghezza attesa dell'UID Microel (sempre 4 byte)

// ─── Costanti blocchi ─────────────────────────────────────────────────────────

#define MICROEL_CREDIT_BLOCK 4      ///< Indice del blocco MIFARE contenente il credito corrente
#define MICROEL_PREV_CREDIT_BLOCK 5 ///< Indice del blocco MIFARE contenente il credito precedente
#define MICROEL_CREDIT_BYTE_LOW 5   ///< Offset del byte basso (LSB) del credito nel blocco
#define MICROEL_CREDIT_BYTE_HIGH 6  ///< Offset del byte alto (MSB) del credito nel blocco

// ─── Costanti menu ricarica ───────────────────────────────────────────────────

/// Numero di importi disponibili nel menu di ricarica
#define MICROEL_CREDIT_OPTIONS_COUNT 5

/// Importi di ricarica in centesimi: 500=5€, 1000=10€, 1500=15€, 2000=20€, 2500=25€
extern const uint16_t MICROEL_CREDIT_OPTIONS[MICROEL_CREDIT_OPTIONS_COUNT];

/// Etichette leggibili per il display (es. " 5.00 EUR", "10.00 EUR")
extern const char *MICROEL_CREDIT_LABELS[MICROEL_CREDIT_OPTIONS_COUNT];

// ─── Struttura dati blocco Microel ───────────────────────────────────────────

/**
 * @brief Rappresentazione decodificata del blocco dati di una tessera Microel.
 *
 * Tutti i valori sono già convertiti da little-endian a host byte order.
 * Il credito è espresso in centesimi (es. 1500 = 15.00 €, max uint16 ≈ 655.35 €).
 *
 * Usata da microelParseBlock() per decodificare e da microelBuildBlock()
 * per serializzare il blocco 4 prima della scrittura sul tag.
 */
struct MicroelBlockData {
    uint16_t operationNumber;       ///< Contatore progressivo delle operazioni (++ad ogni transazione)
    uint16_t totalInputSum;         ///< Somma cumulativa di tutti gli importi caricati
    uint8_t deposit;                ///< Byte deposito (uso specifico del gestore)
    uint16_t credit;                ///< Credito residuo in centesimi (es. 750 = 7.50 €)
    uint32_t transactionDate;       ///< Data/ora ultima transazione (formato proprietario grezzo)
    uint16_t points;                ///< Punti fedeltà accumulati
    uint16_t lastTransactionAmount; ///< Importo dell'ultima transazione in centesimi
    uint8_t checksum;               ///< Checksum di integrità: somma byte 0–14 mod 256
};

// ─── KDF: generazione chiavi ──────────────────────────────────────────────────

/**
 * @brief Genera Key A e Key B Microel da una stringa UID esadecimale.
 *
 * Converte la stringa hex (es. "1E733840") in bytes, poi esegue il KDF.
 * Valida lunghezza (8 chars) e caratteri (solo hex) prima di procedere.
 *
 * @param uidHex Stringa UID in formato hex maiuscolo/minuscolo (es. "1E733840").
 * @param keyA   Buffer output Key A (6 byte).
 * @param keyB   Buffer output Key B (6 byte).
 * @return true se la stringa è valida e le chiavi sono state generate.
 */
bool microelGenerateKeysFromString(const String &uidHex, uint8_t keyA[MICROEL_KEY_LENGTH], uint8_t keyB[MICROEL_KEY_LENGTH]);

/**
 * @brief Salva Key A e Key B per un UID in /rfid/chiavi.txt.
 *
 * Aggiunge due righe nel formato "UID,CHIAVEHEX", compatibile con
 * loadKeysForUID(). Non sovrascrive righe esistenti per lo stesso UID:
 * le nuove righe vengono aggiunte in append.
 *
 * @param uidHex Stringa UID in formato hex maiuscolo (es. "1E733840").
 * @param keyA   Key A da salvare (6 byte).
 * @param keyB   Key B da salvare (6 byte).
 * @return true se il salvataggio è riuscito.
 */
bool microelSaveKeysToSD(const String &uidHex, const uint8_t keyA[MICROEL_KEY_LENGTH], const uint8_t keyB[MICROEL_KEY_LENGTH]);

/**
 * @brief Step 1 del KDF: calcola il valore intermedio sumHex dall'UID.
 *
 * Algoritmo:
 *   1. Somma tutti i byte dell'UID (es. {0xA1, 0xB2, 0xC3, 0xD4} → 0x1AA)
 *   2. Riduce modulo 256 (0x1AA % 256 = 0xAA)
 *   3. Arrotonda al valore pari più vicino (il protocollo richiede parità pari)
 *   4. XOR ogni byte con la chiave fissa xorKey = {0x01,0x92,0xA7,0x75,0x2B,0xF9}
 *
 * Il risultato sumHex viene poi passato a microelGenerateKeyA().
 *
 * @param uid     Puntatore ai byte dell'UID (deve essere di 4 byte).
 * @param uidSize Numero di byte dell'UID (deve essere MICROEL_UID_LENGTH = 4).
 * @param sumHex  Buffer di output di 6 byte con il valore intermedio calcolato.
 */
void microelCalculateSumHex(const uint8_t *uid, size_t uidSize, uint8_t sumHex[MICROEL_KEY_LENGTH]);

/**
 * @brief Step 2 del KDF: genera Key A dall'UID tramite il valore intermedio sumHex.
 *
 * Chiama microelCalculateSumHex() per ottenere sumHex, poi usa il nibble alto
 * del primo byte di sumHex come discriminante per scegliere il secondo XOR:
 *
 *   Nibble 0, 1, 4, 5, 8, 9, C, D → Key A = sumHex (nessun XOR aggiuntivo)
 *   Nibble 2, 3, A, B              → Key A = 0x40 XOR sumHex
 *   Nibble 6, 7, E, F              → Key A = 0xC0 XOR sumHex
 *
 * @param uid     Puntatore ai byte dell'UID.
 * @param uidSize Numero di byte dell'UID.
 * @param keyA    Buffer di output di 6 byte con la Key A generata.
 */
void microelGenerateKeyA(const uint8_t *uid, uint8_t uidSize, uint8_t keyA[MICROEL_KEY_LENGTH]);

/**
 * @brief Step 3 del KDF: genera Key B da Key A tramite NOT bit a bit.
 *
 * La relazione è semplicemente: keyB[i] = 0xFF XOR keyA[i] per ogni byte i.
 * Di conseguenza Key A e Key B sono sempre complementari.
 *
 * @param keyA Buffer di input di 6 byte con la Key A già calcolata.
 * @param keyB Buffer di output di 6 byte con la Key B generata.
 */
void microelGenerateKeyB(const uint8_t keyA[MICROEL_KEY_LENGTH], uint8_t keyB[MICROEL_KEY_LENGTH]);

/**
 * @brief Wrapper che esegue l'intera catena KDF in una sola chiamata.
 *
 * Equivale a chiamare microelGenerateKeyA() seguito da microelGenerateKeyB().
 * Utile quando servono entrambe le chiavi (che è il caso più comune).
 *
 * @param uid     Puntatore ai byte dell'UID.
 * @param uidSize Numero di byte dell'UID (deve essere 4).
 * @param keyA    Buffer di output di 6 byte per la Key A.
 * @param keyB    Buffer di output di 6 byte per la Key B.
 */
void microelGenerateKeys(
    const uint8_t *uid, uint8_t uidSize, uint8_t keyA[MICROEL_KEY_LENGTH], uint8_t keyB[MICROEL_KEY_LENGTH]
);

// ─── Integrazione con MifareDump ─────────────────────────────────────────────

/**
 * @brief Inietta le chiavi KDF Microel in tutti i settori del dump.
 *
 * Genera Key A e Key B dall'UID del dump e le scrive nei campi keyA/keyB
 * di ogni settore. Inietta le chiavi SOLO nei settori che hanno ancora
 * le chiavi di default (FF×6 o 00×6): se un settore ha già una chiave
 * custom, viene lasciato intatto.
 *
 * Chiamata prima di mifareReadDump() o mifareWriteDump() per garantire
 * che le chiavi Microel siano disponibili per l'autenticazione.
 *
 * @param dump Riferimento al dump da aggiornare (tipicamente g_dump).
 */
void microelInjectKeys(MifareDump &dump);

/**
 * @brief Verifica se la tessera in dump è una Microel autentica.
 *
 * Genera le chiavi KDF dall'UID del dump e le confronta con le chiavi
 * già salvate nel dump (keyA[0] e keyB[0]). Se coincidono, la tessera
 * è stata autenticata con chiavi Microel e quindi è Microel.
 *
 * Non legge dati dal tag fisico e non modifica il dump.
 *
 * @param dump Riferimento al dump già autenticato (con keyA/keyB popolati).
 * @return true se le chiavi del dump corrispondono alle chiavi KDF Microel.
 */
bool microelVerify(MifareDump &dump);

/**
 * @brief Legge una tessera Microel e popola g_dump con chiavi e dati.
 *
 * Sequenza:
 *  1. Chiama waitForMifareTag() → attende il tag e popola l'identità in g_dump.
 *  2. Verifica che l'UID sia di 4 byte (requisito Microel).
 *  3. Chiama microelInjectKeys() → inietta le chiavi KDF in g_dump.
 *  4. Chiama mifareReadDump() → legge tutti i settori con le chiavi iniettate.
 *
 * @param sectorsRead Numero di settori letti con successo (output).
 * @return true se la lettura è completata con almeno un settore.
 */
bool microelReadCard(uint8_t &sectorsRead);

// ─── Parser blocco dati ───────────────────────────────────────────────────────

/**
 * @brief Decodifica i 16 byte grezzi del blocco 4 in una struct MicroelBlockData.
 *
 * I valori multi-byte sono letti in little-endian (byte basso per primo),
 * come da formato proprietario Microel. Il checksum non viene verificato
 * qui: usa microelCalculateChecksum() se vuoi verificarlo.
 *
 * @param blockData Puntatore ai 16 byte grezzi del blocco da decodificare.
 * @param out       Struct di output con tutti i campi decodificati.
 */
void microelParseBlock(const uint8_t blockData[16], MicroelBlockData &out);

/**
 * @brief Serializza la struct MicroelBlockData in un buffer a 16 byte.
 *
 * Operazione inversa di microelParseBlock(): converte i campi della struct
 * in little-endian nel buffer grezzo del blocco. Ricalcola automaticamente
 * il checksum (byte 15) come ultimo passo prima di terminare.
 *
 * @param in        Struct con i valori da serializzare.
 * @param blockData Buffer di output di 16 byte da sovrascrivere.
 */
void microelBuildBlock(const MicroelBlockData &in, uint8_t blockData[16]);

/**
 * @brief Calcola il checksum del blocco come somma dei byte 0–14 mod 256.
 *
 * Il byte 15 (checksum stesso) è escluso dal calcolo.
 * Chiamato automaticamente da microelBuildBlock() e microelSetCredit().
 *
 * @param blockData Puntatore ai 16 byte del blocco (il byte 15 è ignorato).
 * @return Valore checksum calcolato (1 byte, 0–255).
 */
uint8_t microelCalculateChecksum(const uint8_t blockData[16]);

// ─── Lettura / modifica credito ───────────────────────────────────────────────

/**
 * @brief Legge il credito corrente dal blocco 4 del dump in centesimi.
 *
 * Legge i byte 5 (LSB) e 6 (MSB) del blocco 4 in little-endian.
 * Restituisce 0 se il blocco 4 non è stato letto (blockRead[4] == false).
 *
 * @param dump Riferimento al dump da cui leggere il credito.
 * @return Credito in centesimi (es. 1500 = 15.00 €), 0 se non disponibile.
 */
uint16_t microelGetCredit(const MifareDump &dump);

/**
 * @brief Aggiorna il credito nel dump in RAM senza scrivere sul tag.
 *
 * Prima di sovrascrivere il credito corrente, lo copia nel blocco 5
 * (credito precedente) per mantenere la storia delle transazioni.
 * Ricalcola il checksum di entrambi i blocchi dopo la modifica.
 *
 * La scrittura fisica sul tag avviene solo con microelWriteCard() o
 * mifareWriteDump() chiamate successivamente.
 *
 * @param dump      Riferimento al dump da modificare (tipicamente g_dump).
 * @param newCredit Nuovo valore del credito in centesimi (es. 1500 = 15.00 €).
 */
void microelSetCredit(MifareDump &dump, uint16_t newCredit);

// ─── Info e output ────────────────────────────────────────────────────────────

/**
 * @brief Stampa su Serial le informazioni di credito della tessera corrente.
 *
 * Output formato: UID, credito corrente, credito precedente.
 * Usata per il debug e per il log durante le operazioni di ricarica.
 *
 * @param dump Riferimento al dump già letto da cui estrarre le informazioni.
 */
void microelPrintCreditInfo(const MifareDump &dump);

/**
 * @brief Legge la tessera Microel e mostra le informazioni chiave su display Bruce.
 *
 * Flusso:
 *  1. Chiama mifareInit() → inizializza il PN532.
 *  2. Mostra "Avvicina la tessera..." a schermo.
 *  3. Chiama waitForMifareTag() → legge UID e identità.
 *  4. Genera Key A e Key B dal KDF (non le legge dal tag fisico).
 *  5. Inietta le chiavi e chiama mifareReadDump() per leggere i blocchi 4–5.
 *  6. Recupera il gestore associato da /rfid/gestori_map.txt.
 *  7. Mostra su display:
 *       - UID del tag (es. "AABBCCDD")
 *       - Key A in formato hex (es. "41D2E735AB39")
 *       - Key B in formato hex (es. "BE2D18CA54C6")
 *       - Credito corrente (es. "15.00 EUR")
 *       - Credito precedente (es. "10.00 EUR")
 *       - Gestore associato (es. "Sto&Bene" oppure "N/A")
 *
 * Non salva nulla su SD. Non modifica g_dump in modo permanente.
 */
void microelInfoCard();

// ─── Scrittura con blocco 0 ────────────────────────────────────────────────────

/**
 * @brief Scrive il dump Microel sul tag fisico incluso il blocco 0.
 *
 * Esegue la scrittura in due fasi:
 *  1. mifareWriteDump()   → scrive tutti i blocchi normali (blocco 0 escluso).
 *  2. mifareWriteBlock0() → scrive il blocco 0 (UID, BCC, dati produttore).
 *
 * La seconda fase (blocco 0) funziona SOLO su tag magic che permettono la
 * sovrascrittura del blocco 0 (es. CUID, FUID, GEN2). Su tag originali
 * MIFARE la scrittura del blocco 0 viene ignorata dall'hardware, ma il
 * resto della scrittura procede normalmente.
 *
 * Il parametro block0Written consente all'UI di informare l'utente se la
 * clonazione è stata completa (tag magic) o parziale (tag originale).
 *
 * @param src            Dump sorgente da scrivere sul tag.
 * @param sectorsWritten Numero di settori scritti con successo (output).
 * @param block0Written  true se il blocco 0 è stato scritto con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
bool microelWriteCard(const MifareDump &src, uint8_t &sectorsWritten, bool &block0Written);

// ─── Menu ricarica ────────────────────────────────────────────────────────────

/**
 * @brief Menu interattivo via Serial per selezionare l'importo di ricarica.
 *
 * Mostra le 5 opzioni numerate (1–5) su Serial Monitor e attende input
 * con timeout di 30 secondi. Restituisce 0 se l'utente digita 0,
 * inserisce un valore non valido, o scade il timeout.
 *
 * @return Importo selezionato in centesimi, oppure 0 se annullato/timeout.
 */
uint16_t microelCreditMenu();

/**
 * @brief Flusso completo di ricarica: leggi → scegli importo → conferma → scrivi.
 *
 * Sequenza operativa con 5 step:
 *  1. Attende la tessera Microel sul lettore.
 *  2. Legge tutti i settori con chiavi KDF.
 *  3. Mostra il credito corrente su Serial.
 *  4. Chiede all'utente di selezionare il nuovo importo tramite microelCreditMenu().
 *  5. Chiede conferma (s/n con timeout 15 secondi).
 *  6. Chiama microelSetCredit() per aggiornare il credito in RAM.
 *  7. Chiama microelWriteCard() per scrivere sul tag fisico (incluso blocco 0).
 *
 * In caso di errore in qualsiasi step, stampa un messaggio su Serial e
 * ritorna false senza modificare il tag fisico.
 *
 * @return true se la ricarica è andata a buon fine, false altrimenti.
 */
bool microelRechargeTessera();
