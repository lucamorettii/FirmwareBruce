#pragma once

/**
 * @file TessereMicroel.h
 * @brief Logica KDF e gestione credito per tessere MIFARE Microel.
 *
 * Estende TessereLogica per le tessere Microel, sistema di pagamento
 * prepagato su MIFARE Classic 1K usato in Italia su distributori e accessi.
 *
 * Funzionalità:
 *
 *  1. KDF (Key Derivation Function)
 *     Key A e Key B vengono derivate dall'UID in 3 step:
 *       UID → calcolaSumHex → generaChiaveA → generaChiaveB (= NOT di A)
 *
 *  2. Parser blocco dati (blocco 4)
 *     Blocco da 16 byte little-endian: numero operazione, credito,
 *     data transazione, punti, checksum.
 *
 *  3. Info card su display
 *     Mostra UID, Key A/B, credito corrente/precedente, gestore, SD.
 *
 *  4. Scrittura con blocco 0
 *     Scrive dump completo incluso blocco 0 per clonazione su tag magic.
 *
 * Struttura blocco 4 (16 byte, little-endian):
 *   Byte  0–1  → Numero operazione (uint16)
 *   Byte  2–3  → Somma totale input (uint16)
 *   Byte  4    → Deposito (uint8)
 *   Byte  5–6  → Credito corrente in centesimi (uint16)
 *   Byte  7–10 → Data ultima transazione (uint32)
 *   Byte 11–12 → Punti accumulati (uint16)
 *   Byte 13–14 → Importo ultima transazione in centesimi (uint16)
 *   Byte 15    → Checksum (somma byte 0–14 mod 256)
 */

#include "../TessereLogica.h"
#include <Arduino.h>

// ─── Costanti KDF ─────────────────────────────────────────────────────────────

#define LUNGHEZZA_CHIAVE 6 ///< Lunghezza in byte di Key A e Key B (standard MIFARE)
#define LUNGHEZZA_UID 4    ///< Lunghezza attesa dell'UID Microel (sempre 4 byte)

// ─── Costanti blocchi ─────────────────────────────────────────────────────────

#define BLOCCO_CREDITO 4      ///< Indice del blocco MIFARE con il credito corrente
#define BLOCCO_CREDITO_PREC 5 ///< Indice del blocco MIFARE con il credito precedente
#define BYTE_CREDITO_BASSO 5  ///< Offset del byte LSB del credito nel blocco
#define BYTE_CREDITO_ALTO 6   ///< Offset del byte MSB del credito nel blocco

// ─── Struttura dati blocco Microel ───────────────────────────────────────────

/**
 * @brief Rappresentazione decodificata del blocco dati di una tessera Microel.
 *
 * Tutti i valori sono già convertiti da little-endian a host byte order.
 * Il credito è in centesimi (es. 1500 = 15.00 €, max uint16 ≈ 655.35 €).
 */
struct DatiBloccoMicroel {
    uint16_t numeroOperazione;    ///< Contatore progressivo (++ad ogni transazione)
    uint16_t totaleCarichiInput;  ///< Somma cumulativa di tutti gli importi caricati
    uint8_t deposito;             ///< Byte deposito (uso specifico del gestore)
    uint16_t credito;             ///< Credito residuo in centesimi (es. 750 = 7.50 €)
    uint32_t dataTransazione;     ///< Data/ora ultima transazione (formato grezzo)
    uint16_t punti;               ///< Punti fedeltà accumulati
    uint16_t importoUltimaOperaz; ///< Importo dell'ultima transazione in centesimi
    uint8_t checksum;             ///< Checksum: somma byte 0–14 mod 256
};

// ─── KDF: generazione chiavi ──────────────────────────────────────────────────

/**
 * @brief Genera Key A e Key B da una stringa UID esadecimale (es. "1E733840").
 *
 * Valida lunghezza (8 chars) e caratteri hex prima di procedere.
 *
 * @param uidHex Stringa UID hex maiuscola/minuscola.
 * @param chiaveA Buffer output Key A (6 byte).
 * @param chiaveB Buffer output Key B (6 byte).
 * @return true se la stringa è valida e le chiavi sono state generate.
 */
bool generaChiaviDaStringa(const String &uidHex, uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]);

/**
 * @brief Salva Key A e Key B per un UID in PERCORSO_CHIAVI in append.
 *
 * Non sovrascrive righe esistenti per lo stesso UID.
 *
 * @return true se il salvataggio è riuscito.
 */
bool salvaChiaviSD(const String &uidHex, const uint8_t chiaveA[LUNGHEZZA_CHIAVE], const uint8_t chiaveB[LUNGHEZZA_CHIAVE]);

/**
 * @brief Step 1 KDF: calcola il valore intermedio sumHex dall'UID.
 *
 * Algoritmo:
 *   1. Somma i 4 byte dell'UID → riduce mod 256 → arrotonda al pari più vicino
 *   2. XOR ogni byte con la chiave fissa {0x01,0x92,0xA7,0x75,0x2B,0xF9}
 *
 * @param uid     Puntatore ai byte dell'UID (4 byte).
 * @param dim     Numero di byte dell'UID.
 * @param sumHex  Buffer output di 6 byte con il valore intermedio.
 */
void calcolaSumHex(const uint8_t *uid, size_t dim, uint8_t sumHex[LUNGHEZZA_CHIAVE]);

/**
 * @brief Step 2 KDF: genera Key A dall'UID tramite sumHex.
 *
 * Il nibble alto del primo byte di sumHex determina il secondo XOR:
 *   - 2,3,A,B → Key A = 0x40 XOR sumHex
 *   - 6,7,E,F → Key A = 0xC0 XOR sumHex
 *   - Tutti gli altri → Key A = sumHex
 */
void generaChiaveA(const uint8_t *uid, uint8_t dim, uint8_t chiaveA[LUNGHEZZA_CHIAVE]);

/**
 * @brief Step 3 KDF: genera Key B come NOT bit a bit di Key A.
 *
 * chiaveB[i] = 0xFF XOR chiaveA[i] per ogni byte i.
 */
void generaChiaveB(const uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]);

/**
 * @brief Wrapper KDF completo: UID → Key A + Key B in una sola chiamata.
 */
void generaChiavi(const uint8_t *uid, uint8_t dim, uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]);

// ─── Integrazione con DumpMifare ─────────────────────────────────────────────

/**
 * @brief Inietta le chiavi KDF Microel nei settori del dump con chiavi di default.
 *
 * Sovrascrive solo i settori che hanno ancora chiavi FF×6 o 00×6.
 * I settori con chiavi personalizzate restano intatti.
 * Da chiamare prima di leggiDump() o scriviDump() su tessere Microel.
 */
void iniettaChiavi(DumpMifare &dump);

/**
 * @brief Legge la tessera Microel presente sul lettore e popola dump_globale.
 *
 * Sequenza: attesaTag() → iniettaChiavi() → leggiDumpConChiavi()
 *
 * @param settoriLetti Numero di settori letti (output).
 * @return true se la lettura è completata con almeno un settore.
 */
bool leggiTesseraMicroel(uint8_t &settoriLetti);

// ─── Parser blocco dati ───────────────────────────────────────────────────────

/**
 * @brief Decodifica i 16 byte grezzi del blocco 4 in una struct DatiBloccoMicroel.
 *
 * Tutti i valori multi-byte sono letti in little-endian.
 */
void decodificaBlocco(const uint8_t dati[16], DatiBloccoMicroel &out);

/**
 * @brief Serializza la struct DatiBloccoMicroel in un buffer a 16 byte.
 *
 * Operazione inversa di decodificaBlocco(). Ricalcola automaticamente
 * il checksum (byte 15) prima di terminare.
 */
void costruisciBlocco(const DatiBloccoMicroel &in, uint8_t dati[16]);

/**
 * @brief Calcola il checksum come somma dei byte 0–14 mod 256.
 *
 * Il byte 15 (checksum stesso) è escluso dal calcolo.
 */
uint8_t calcolaChecksum(const uint8_t dati[16]);

// ─── Lettura / modifica credito ───────────────────────────────────────────────

/**
 * @brief Aggiorna credito con storico completo coerente (logica Microel verificata).
 *
 * B5 ← stato attuale di B4 (diventa il "precedente")
 * B4 ← nuovo stato con credito sostituito, op++, totale aggiornato
 * B6 ← copia esatta di B4
 * Data fissa al valore del protocollo Microel verificato su dump reali.
 *
 * @param dump         Dump da modificare in RAM.
 * @param nuovoCredito Nuovo credito in centesimi (sostituzione, non aggiunta).
 */
void impostaCreditoCompleto(DumpMifare &dump, uint16_t nuovoCredito);

/**
 * @brief Legge il credito corrente dal blocco 4 del dump in centesimi.
 *
 * @return Credito in centesimi (es. 1500 = 15.00 €), 0 se blocco non letto.
 */
uint16_t leggiCredito(const DumpMifare &dump);

// ─── Info e scrittura ────────────────────────────────────────────────────────

/**
 * @brief Legge la tessera Microel e mostra UID, Key A/B, credito, gestore e SD su display.
 *
 * Genera le chiavi KDF dall'UID senza autenticarsi prima, poi le usa per
 * leggere i blocchi 4 e 5. Mostra anche se esiste un dump salvato su SD.
 * Non salva nulla: operazione di sola lettura/visualizzazione.
 */
void infoTesseraMicroel();

/**
 * @brief Scrive il dump Microel sul tag fisico incluso il blocco 0.
 *
 * Fase 1: scriviDump()  → tutti i blocchi normali (blocco 0 escluso).
 * Fase 2: scriviBloc0() → blocco 0 (funziona solo su tag magic).
 *
 * @param src             Dump sorgente.
 * @param settoriScritti  Numero di settori scritti (output).
 * @param blocco0Scritto  true se il blocco 0 è stato scritto (output).
 * @return true se almeno un settore è stato scritto.
 */
bool scriviTesseraMicroel(const DumpMifare &src, uint8_t &settoriScritti, bool &blocco0Scritto);
