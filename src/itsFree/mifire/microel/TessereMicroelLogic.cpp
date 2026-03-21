/**
 * @file TessereMicroelLogic.cpp
 * @brief Implementazione della logica KDF e gestione credito per tessere Microel.
 *
 * Vedi TessereMicroelLogic.h per la documentazione completa delle API.
 *
 * Note implementative importanti:
 *  - Il credito è sempre gestito in centesimi (uint16_t).
 *    Max = 65535 centesimi = 655.35 €. Verificare overflow per importi grandi.
 *  - microelSetCredit() aggiorna sia il blocco 4 (credito corrente) sia il
 *    blocco 5 (credito precedente) per mantenere la storia delle transazioni.
 *  - Il checksum (byte 15 di ogni blocco) viene sempre ricalcolato prima
 *    della scrittura per garantire l'integrità del dato lato terminale.
 *  - microelInfoCard() genera le chiavi dal KDF senza autenticarsi prima:
 *    le chiavi vengono mostrate all'utente e poi usate per leggere il credito.
 *
 * Modifiche rispetto alla versione originale:
 *  - Aggiunta implementazione di microelInfoCard() con output su display Bruce.
 *  - Aggiunta implementazione di microelWriteCard() con chiamata a mifareWriteBlock0().
 *  - Aggiunti helper UI showMessage() e showInfo() per non dipendere da TessereMenuLogic.cpp.
 */

#include "TessereMicroelLogic.h"
#include "core/display.h"    // drawMainBorderWithTitle, padprintln, setPadCursor
#include "core/mykeyboard.h" // AnyKeyPress, InputHandler

// ─── Tabelle menu ricarica ────────────────────────────────────────────────────

/// Importi disponibili nel menu di ricarica, in centesimi.
/// Indice 0 = 5.00 €, indice 1 = 10.00 €, ..., indice 4 = 25.00 €.
const uint16_t MICROEL_CREDIT_OPTIONS[MICROEL_CREDIT_OPTIONS_COUNT] = {
    500,  //  5.00 €
    1000, // 10.00 €
    1500, // 15.00 €
    2000, // 20.00 €
    2500  // 25.00 €
};

/// Etichette formattate per il display, allineate a destra con spazio iniziale per 5 €.
const char *MICROEL_CREDIT_LABELS[MICROEL_CREDIT_OPTIONS_COUNT] = {
    " 5.00 EUR", "10.00 EUR", "15.00 EUR", "20.00 EUR", "25.00 EUR"
};

// ─── Helper UI (statici interni) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Ridefinito qui per evitare dipendenze circolari con TessereMenuLogic.cpp.
 * Identico all'helper presente in quel file.
 *
 * @param title Titolo del pannello (barra superiore).
 * @param body  Testo da mostrare, separato da '\n' per le righe.
 */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);
    String tmp = body;
    int start = 0, idx;
    // Scorre il testo riga per riga usando '\n' come separatore
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx));
        start = idx + 1;
    }
    padprintln(tmp.substring(start)); // stampa l'ultima riga (priva di '\n')
    delay(300);                       // pausa minima per evitare lettura immediata
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

/**
 * @brief Mostra un pannello con titolo e corpo testuale senza attendere input.
 *
 * Usata per i messaggi di stato intermedi (es. "Lettura in corso...",
 * "Avvicina la tessera...") durante le operazioni NFC.
 * Non blocca: il flusso del programma continua immediatamente.
 *
 * @param title Titolo del pannello.
 * @param body  Testo da mostrare.
 */
static void showInfo(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);
    String tmp = body;
    int start = 0, idx;
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx));
        start = idx + 1;
    }
    padprintln(tmp.substring(start));
    // Nessuna attesa: ritorna subito dopo aver disegnato il pannello
}

// ─── KDF: implementazione ─────────────────────────────────────────────────────

/**
 * @brief Step 1 KDF: calcola il valore intermedio sumHex dall'UID.
 *
 * Il valore sumHex è un array di 6 byte ottenuto come segue:
 *   1. Somma i 4 byte dell'UID (es. {0xA1,0xB2,0xC3,0xD4} → 298 = 0x12A)
 *   2. Riduce modulo 256 (0x12A % 256 = 0x2A = 42)
 *   3. Se il risultato è dispari, lo incrementa di 2 per ottenere parità pari
 *      (es. 42 è pari → rimane 42; 43 dispari → diventa 45)
 *   4. XOR ogni byte con xorKey fissa per ottenere i 6 byte di sumHex
 *
 * La chiave xorKey è fissa per tutte le tessere Microel:
 *   {0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9}
 */
void microelCalculateSumHex(const uint8_t *uid, size_t uidSize, uint8_t sumHex[MICROEL_KEY_LENGTH]) {
    // Chiave XOR fissa del protocollo Microel (costante per tutte le tessere)
    const uint8_t xorKey[MICROEL_KEY_LENGTH] = {0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9};

    // Step 1: somma tutti i byte dell'UID
    int sum = 0;
    for (size_t i = 0; i < uidSize; i++) sum += uid[i];

    // Step 2: riduzione modulo 256 (prende solo il byte basso della somma)
    int sumTwoDigits = sum % 256;

    // Step 3: il protocollo Microel richiede un valore pari
    // Se dispari, incrementa di 2 (non di 1!) per mantenere la parità
    if (sumTwoDigits % 2 == 1) sumTwoDigits += 2;

    // Step 4: XOR con ogni byte della chiave fissa per produrre sumHex
    for (size_t i = 0; i < MICROEL_KEY_LENGTH; i++) sumHex[i] = (uint8_t)(sumTwoDigits ^ xorKey[i]);
}

/**
 * @brief Step 2 KDF: genera Key A dal valore intermedio sumHex.
 *
 * Il nibble alto del primo byte di sumHex (i 4 bit più significativi)
 * determina quale variante di XOR applicare:
 *
 *   Nibble 2,3,A,B → XOR con 0x40 (variante 1)
 *   Nibble 6,7,E,F → XOR con 0xC0 (variante 2)
 *   Tutti gli altri → Key A = sumHex senza XOR aggiuntivo (variante 3)
 *
 * Questo meccanismo introduce una seconda fonte di variabilità oltre alla
 * somma dell'UID, rendendo più difficile la derivazione inversa delle chiavi.
 */
void microelGenerateKeyA(const uint8_t *uid, uint8_t uidSize, uint8_t keyA[MICROEL_KEY_LENGTH]) {
    uint8_t sumHex[MICROEL_KEY_LENGTH];
    microelCalculateSumHex(uid, uidSize, sumHex); // calcola il valore intermedio

    // Estrae il nibble alto del primo byte (i 4 bit più significativi)
    uint8_t firstNibble = (sumHex[0] >> 4) & 0x0F;

    if (firstNibble == 0x2 || firstNibble == 0x3 || firstNibble == 0xA || firstNibble == 0xB) {
        // Variante 1: secondo XOR con 0x40
        for (size_t i = 0; i < MICROEL_KEY_LENGTH; i++) keyA[i] = 0x40 ^ sumHex[i];

    } else if (firstNibble == 0x6 || firstNibble == 0x7 || firstNibble == 0xE || firstNibble == 0xF) {
        // Variante 2: secondo XOR con 0xC0
        for (size_t i = 0; i < MICROEL_KEY_LENGTH; i++) keyA[i] = 0xC0 ^ sumHex[i];

    } else {
        // Variante 3: nessun XOR aggiuntivo, Key A coincide con sumHex
        for (size_t i = 0; i < MICROEL_KEY_LENGTH; i++) keyA[i] = sumHex[i];
    }
}

/**
 * @brief Step 3 KDF: genera Key B come complemento a 1 di Key A.
 *
 * La relazione keyB[i] = 0xFF XOR keyA[i] vale per ogni byte i.
 * Di conseguenza: keyA XOR keyB = 0xFF×6 = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}.
 * Questo permette di verificare la correttezza: se keyA XOR keyB ≠ 0xFF×6,
 * almeno una delle due chiavi è errata.
 */
void microelGenerateKeyB(const uint8_t keyA[MICROEL_KEY_LENGTH], uint8_t keyB[MICROEL_KEY_LENGTH]) {
    // NOT bit a bit di ogni byte di Key A
    for (size_t i = 0; i < MICROEL_KEY_LENGTH; i++) keyB[i] = 0xFF ^ keyA[i];
}

/**
 * @brief Wrapper KDF completo: UID → Key A + Key B in una sola chiamata.
 *
 * Chiama microelGenerateKeyA() e poi microelGenerateKeyB().
 * È la funzione da usare quando servono entrambe le chiavi.
 */
void microelGenerateKeys(
    const uint8_t *uid, uint8_t uidSize, uint8_t keyA[MICROEL_KEY_LENGTH], uint8_t keyB[MICROEL_KEY_LENGTH]
) {
    microelGenerateKeyA(uid, uidSize, keyA); // Step 1+2: UID → Key A
    microelGenerateKeyB(keyA, keyB);         // Step 3: Key A → Key B
}

// ─── Integrazione con MifareDump ─────────────────────────────────────────────

/**
 * @brief Inietta le chiavi KDF Microel nei settori del dump con chiavi default.
 *
 * Genera Key A e Key B dall'UID del dump, poi sovrascrive le chiavi di ogni
 * settore che ha ancora le chiavi di default (FF×6 o 00×6). I settori con
 * chiavi custom (es. settori con chiavi diverse su tessere ibride) vengono
 * lasciati intatti.
 *
 * Deve essere chiamata prima di mifareReadDump() o mifareWriteDump() per
 * permettere al driver PN532 di autenticarsi sui settori Microel.
 */
void microelInjectKeys(MifareDump &dump) {
    // Controllo preliminare: Microel usa solo UID di 4 byte
    if (dump.uidLen != MICROEL_UID_LENGTH) {
        Serial.println("[Microel] WARN: UID non è di 4 byte, chiavi non iniettate.");
        return;
    }

    // Genera le chiavi KDF dall'UID del dump
    uint8_t keyA[MICROEL_KEY_LENGTH], keyB[MICROEL_KEY_LENGTH];
    microelGenerateKeys(dump.uid, dump.uidLen, keyA, keyB);

    // Chiavi "placeholder" da sostituire (quelle impostate prima della lettura reale)
    const uint8_t defaultFF[MICROEL_KEY_LENGTH] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t default00[MICROEL_KEY_LENGTH] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    for (uint8_t s = 0; s < dump.numSectors; s++) {
        // Sostituisce Key A solo se è ancora la chiave di default (FF o 00)
        if (memcmp(dump.keyA[s], defaultFF, MICROEL_KEY_LENGTH) == 0 ||
            memcmp(dump.keyA[s], default00, MICROEL_KEY_LENGTH) == 0) {
            memcpy(dump.keyA[s], keyA, MICROEL_KEY_LENGTH);
            dump.keyAFound[s] = true; // segnala che la chiave è ora valida
        }

        // Sostituisce Key B solo se è ancora la chiave di default (FF o 00)
        if (memcmp(dump.keyB[s], defaultFF, MICROEL_KEY_LENGTH) == 0 ||
            memcmp(dump.keyB[s], default00, MICROEL_KEY_LENGTH) == 0) {
            memcpy(dump.keyB[s], keyB, MICROEL_KEY_LENGTH);
            dump.keyBFound[s] = true;
        }
    }

    Serial.printf(
        "[Microel] Chiavi KDF iniettate per UID %02X%02X%02X%02X\n",
        dump.uid[0],
        dump.uid[1],
        dump.uid[2],
        dump.uid[3]
    );
}

/**
 * @brief Verifica se il dump corrisponde a una tessera Microel.
 *
 * Confronta le chiavi salvate nel dump (settore 0) con quelle che il KDF
 * genererebbe per lo stesso UID. Se coincidono, la tessera è stata
 * autenticata con le chiavi Microel → è una tessera Microel.
 *
 * Non fa nessuna lettura fisica dal tag.
 */
bool microelVerify(MifareDump &dump) {
    if (dump.uidLen != MICROEL_UID_LENGTH) return false; // UID non Microel

    // Genera le chiavi attese per questo UID tramite KDF
    uint8_t keyA[MICROEL_KEY_LENGTH], keyB[MICROEL_KEY_LENGTH];
    microelGenerateKeys(dump.uid, dump.uidLen, keyA, keyB);

    // Confronta Key A del settore 0 con quella attesa
    if (dump.keyAFound[0] && memcmp(dump.keyA[0], keyA, MICROEL_KEY_LENGTH) == 0) {
        Serial.println("[Microel] Verifica OK (keyA): tessera Microel.");
        return true;
    }

    // Confronta Key B del settore 0 con quella attesa (fallback)
    if (dump.keyBFound[0] && memcmp(dump.keyB[0], keyB, MICROEL_KEY_LENGTH) == 0) {
        Serial.println("[Microel] Verifica OK (keyB): tessera Microel.");
        return true;
    }

    Serial.println("[Microel] Verifica fallita: tessera non Microel.");
    return false;
}

/**
 * @brief Legge la tessera Microel presente sul lettore e popola g_dump.
 *
 * Combina waitForMifareTag() + microelInjectKeys() + mifareReadDump()
 * in un'unica funzione conveniente. Non mostra messaggi a schermo:
 * il chiamante è responsabile di mostrare l'avanzamento se necessario.
 */
bool microelReadCard(uint8_t &sectorsRead) {
    if (!waitForMifareTag()) {
        Serial.println("[Microel] Nessun tag rilevato.");
        return false;
    }

    if (g_dump.uidLen != MICROEL_UID_LENGTH) {
        Serial.printf("[Microel] UID di %d byte, attesi 4.\n", g_dump.uidLen);
        return false;
    }

    Serial.printf(
        "[Microel] Tag: %02X%02X%02X%02X\n", g_dump.uid[0], g_dump.uid[1], g_dump.uid[2], g_dump.uid[3]
    );

    // Inietta le chiavi KDF nei settori con chiavi di default
    microelInjectKeys(g_dump);

    // USA mifareReadDumpWithKeys invece di mifareReadDump:
    // le chiavi sono già in g_dump.keyA/keyB, non vanno caricate dalla SD
    bool ok = mifareReadDumpWithKeys(sectorsRead);
    if (!ok) Serial.println("[Microel] Lettura fallita.");
    else Serial.printf("[Microel] Letti %d settori.\n", sectorsRead);

    return ok;
}

// ─── Parser blocco dati ───────────────────────────────────────────────────────

/**
 * @brief Decodifica i 16 byte grezzi del blocco 4 in una struct MicroelBlockData.
 *
 * Tutti i valori multi-byte sono codificati in little-endian (byte basso prima).
 * Esempio per il credito: blockData[5]=0xF4, blockData[6]=0x01 → 0x01F4 = 500 centesimi = 5.00 €.
 *
 * Layout completo:
 *   [0]    LSB operationNumber
 *   [1]    MSB operationNumber
 *   [2]    LSB totalInputSum
 *   [3]    MSB totalInputSum
 *   [4]    deposit
 *   [5]    LSB credit (byte basso del credito in centesimi)
 *   [6]    MSB credit (byte alto del credito in centesimi)
 *   [7]    byte 0 transactionDate (LSB)
 *   [8]    byte 1 transactionDate
 *   [9]    byte 2 transactionDate
 *   [10]   byte 3 transactionDate (MSB)
 *   [11]   LSB points
 *   [12]   MSB points
 *   [13]   LSB lastTransactionAmount
 *   [14]   MSB lastTransactionAmount
 *   [15]   checksum
 */
void microelParseBlock(const uint8_t blockData[16], MicroelBlockData &out) {
    // Numero operazione: byte 0 (LSB) e byte 1 (MSB)
    out.operationNumber = (uint16_t)(blockData[0] | (blockData[1] << 8));

    // Somma totale input: byte 2 (LSB) e byte 3 (MSB)
    out.totalInputSum = (uint16_t)(blockData[2] | (blockData[3] << 8));

    // Deposito: byte singolo
    out.deposit = blockData[4];

    // Credito corrente in centesimi: byte 5 (LSB) e byte 6 (MSB)
    out.credit = (uint16_t)(blockData[5] | (blockData[6] << 8));

    // Data transazione: 4 byte little-endian (byte 7–10)
    out.transactionDate =
        (uint32_t)(blockData[7] | (blockData[8] << 8) | (blockData[9] << 16) | (blockData[10] << 24));

    // Punti: byte 11 (LSB) e byte 12 (MSB)
    out.points = (uint16_t)(blockData[11] | (blockData[12] << 8));

    // Importo ultima transazione: byte 13 (LSB) e byte 14 (MSB)
    out.lastTransactionAmount = (uint16_t)(blockData[13] | (blockData[14] << 8));

    // Checksum (byte 15): non verificato qui, solo copiato
    out.checksum = blockData[15];
}

/**
 * @brief Serializza la struct MicroelBlockData nel buffer a 16 byte.
 *
 * Operazione inversa di microelParseBlock(). Converte tutti i campi
 * in little-endian nel buffer. Come ultimo passo ricalcola il checksum
 * e lo scrive nel byte 15 per mantenere l'integrità.
 */
void microelBuildBlock(const MicroelBlockData &in, uint8_t blockData[16]) {
    blockData[0] = (uint8_t)(in.operationNumber & 0xFF);          // LSB numero operazione
    blockData[1] = (uint8_t)(in.operationNumber >> 8);            // MSB numero operazione
    blockData[2] = (uint8_t)(in.totalInputSum & 0xFF);            // LSB somma totale
    blockData[3] = (uint8_t)(in.totalInputSum >> 8);              // MSB somma totale
    blockData[4] = in.deposit;                                    // byte deposito
    blockData[5] = (uint8_t)(in.credit & 0xFF);                   // LSB credito
    blockData[6] = (uint8_t)(in.credit >> 8);                     // MSB credito
    blockData[7] = (uint8_t)(in.transactionDate & 0xFF);          // byte 0 data
    blockData[8] = (uint8_t)((in.transactionDate >> 8) & 0xFF);   // byte 1 data
    blockData[9] = (uint8_t)((in.transactionDate >> 16) & 0xFF);  // byte 2 data
    blockData[10] = (uint8_t)((in.transactionDate >> 24) & 0xFF); // byte 3 data
    blockData[11] = (uint8_t)(in.points & 0xFF);                  // LSB punti
    blockData[12] = (uint8_t)(in.points >> 8);                    // MSB punti
    blockData[13] = (uint8_t)(in.lastTransactionAmount & 0xFF);   // LSB ultima transazione
    blockData[14] = (uint8_t)(in.lastTransactionAmount >> 8);     // MSB ultima transazione

    // Ricalcola e scrive il checksum come ultimo passo (dipende dai byte 0–14)
    blockData[15] = microelCalculateChecksum(blockData);
}

/**
 * @brief Calcola il checksum come somma dei byte 0–14 mod 256.
 *
 * Il byte 15 (checksum stesso) NON è incluso nel calcolo.
 * La somma è accumulata in uint16_t per evitare overflow prima del modulo.
 */
uint8_t microelCalculateChecksum(const uint8_t blockData[16]) {
    uint16_t sum = 0;
    for (int i = 0; i < 15; i++) sum += blockData[i]; // somma byte 0–14
    return (uint8_t)(sum % 256);                      // riduce al byte basso
}

// ─── Lettura e modifica credito ───────────────────────────────────────────────

/**
 * @brief Legge il credito corrente dal blocco 4 del dump in centesimi.
 *
 * I byte del credito sono: blockData[5] = LSB, blockData[6] = MSB (little-endian).
 * Esempio: byte[5]=0x08, byte[6]=0x07 → 0x0708 = 1800 centesimi = 18.00 €.
 */
uint16_t microelGetCredit(const MifareDump &dump) {
    // Verifica che il blocco sia stato letto con successo durante la Read
    if (!dump.blockRead[MICROEL_CREDIT_BLOCK]) {
        Serial.println("[Microel] WARN: blocco credito (4) non letto.");
        return 0;
    }
    const uint8_t *b = dump.data[MICROEL_CREDIT_BLOCK];
    // Ricostruisce il uint16_t little-endian dai due byte
    return (uint16_t)(b[MICROEL_CREDIT_BYTE_LOW] | (b[MICROEL_CREDIT_BYTE_HIGH] << 8));
}

/**
 * @brief Imposta il nuovo credito nel dump in RAM aggiornando blocco 4 e blocco 5.
 *
 * Sequenza di aggiornamento:
 *  1. Legge il credito corrente dal blocco 4.
 *  2. Lo copia nel blocco 5 (credito precedente) per mantenere la storia.
 *  3. Ricalcola e aggiorna il checksum del blocco 5.
 *  4. Scrive il nuovo credito nel blocco 4 (byte 5 LSB, byte 6 MSB).
 *  5. Ricalcola e aggiorna il checksum del blocco 4.
 *
 * Nessuna scrittura fisica avviene qui: serve una chiamata successiva
 * a microelWriteCard() o mifareWriteDump().
 */
void microelSetCredit(MifareDump &dump, uint16_t newCredit) {
    // Aggiorna il blocco 5 con il credito attuale (diventa il credito "precedente")
    if (dump.blockRead[MICROEL_CREDIT_BLOCK] && dump.blockRead[MICROEL_PREV_CREDIT_BLOCK]) {
        uint16_t currentCredit = microelGetCredit(dump); // legge il credito attuale

        // Scrive il credito attuale nel blocco precedente (little-endian)
        dump.data[MICROEL_PREV_CREDIT_BLOCK][MICROEL_CREDIT_BYTE_LOW] =
            (uint8_t)(currentCredit & 0xFF);                                                            // LSB
        dump.data[MICROEL_PREV_CREDIT_BLOCK][MICROEL_CREDIT_BYTE_HIGH] = (uint8_t)(currentCredit >> 8); // MSB

        // Ricalcola il checksum del blocco 5 dopo la modifica
        dump.data[MICROEL_PREV_CREDIT_BLOCK][15] =
            microelCalculateChecksum(dump.data[MICROEL_PREV_CREDIT_BLOCK]);
    }

    // Scrive il nuovo credito nel blocco 4 (little-endian)
    dump.data[MICROEL_CREDIT_BLOCK][MICROEL_CREDIT_BYTE_LOW] = (uint8_t)(newCredit & 0xFF); // LSB
    dump.data[MICROEL_CREDIT_BLOCK][MICROEL_CREDIT_BYTE_HIGH] = (uint8_t)(newCredit >> 8);  // MSB

    // Ricalcola il checksum del blocco 4 dopo la modifica
    dump.data[MICROEL_CREDIT_BLOCK][15] = microelCalculateChecksum(dump.data[MICROEL_CREDIT_BLOCK]);

    Serial.printf("[Microel] Credito aggiornato: %d.%02d EUR\n", newCredit / 100, newCredit % 100);
}

// ─── Output informativo su Serial ────────────────────────────────────────────

/**
 * @brief Stampa le informazioni di credito della tessera su Serial Monitor.
 *
 * Usata per il log e il debug durante le operazioni di ricarica.
 * Stampa: UID, credito corrente (blocco 4) e credito precedente (blocco 5).
 */
void microelPrintCreditInfo(const MifareDump &dump) {
    Serial.println("[Microel] ─────────────────────────");
    Serial.print("[Microel] UID: ");
    for (uint8_t i = 0; i < dump.uidLen; i++) Serial.printf("%02X ", dump.uid[i]);
    Serial.println();

    // Stampa il credito corrente se il blocco 4 è stato letto
    if (dump.blockRead[MICROEL_CREDIT_BLOCK]) {
        uint16_t c = microelGetCredit(dump);
        Serial.printf("[Microel] Credito corrente:   %d.%02d EUR\n", c / 100, c % 100);
    } else {
        Serial.println("[Microel] Credito corrente:   N/D");
    }

    // Stampa il credito precedente se il blocco 5 è stato letto
    if (dump.blockRead[MICROEL_PREV_CREDIT_BLOCK]) {
        const uint8_t *b = dump.data[MICROEL_PREV_CREDIT_BLOCK];
        uint16_t p = (uint16_t)(b[MICROEL_CREDIT_BYTE_LOW] | (b[MICROEL_CREDIT_BYTE_HIGH] << 8));
        Serial.printf("[Microel] Credito precedente: %d.%02d EUR\n", p / 100, p % 100);
    }
    Serial.println("[Microel] ─────────────────────────");
}

// ─── Info card su display Bruce ───────────────────────────────────────────────

/**
 * @brief Legge la tessera Microel e mostra tutte le info chiave su display.
 *
 * Genera le chiavi KDF dall'UID senza prima autenticarsi, poi le usa per
 * leggere i blocchi 4 e 5 (credito corrente e precedente).
 * Recupera il gestore dalla mappa su SD.
 * Mostra tutto su display in un pannello formattato.
 *
 * Nessun salvataggio su SD: operazione di sola lettura/visualizzazione.
 */
void microelInfoCard() {
    if (!mifareInit()) return; // inizializza PN532 se necessario

    // Mostra messaggio di attesa mentre si aspetta il tag
    showInfo("Microel Info", "Avvicina la tessera\nMicroel al lettore...");

    // Attende il tag e popola g_dump con l'identità
    if (!waitForMifareTag()) return;

    // Verifica che l'UID sia di 4 byte (requisito Microel)
    if (g_dump.uidLen != MICROEL_UID_LENGTH) {
        showMessage("Microel Info", "UID non valido.\nAttesi 4 byte.\nNon e' una tessera\nMicroel.");
        return;
    }

    // Costruisce la stringa UID per il display e per la ricerca gestore
    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);

    // Genera Key A e Key B tramite KDF (non lette dal tag: derivate dall'UID)
    uint8_t keyA[MICROEL_KEY_LENGTH], keyB[MICROEL_KEY_LENGTH];
    microelGenerateKeys(g_dump.uid, g_dump.uidLen, keyA, keyB);

    // Converte Key A in stringa hex per il display
    String keyAStr;
    for (int i = 0; i < MICROEL_KEY_LENGTH; i++) {
        if (keyA[i] < 0x10) keyAStr += '0'; // padding per byte < 0x10
        keyAStr += String(keyA[i], HEX);
    }
    keyAStr.toUpperCase(); // normalizza a maiuscolo

    // Converte Key B in stringa hex per il display
    String keyBStr;
    for (int i = 0; i < MICROEL_KEY_LENGTH; i++) {
        if (keyB[i] < 0x10) keyBStr += '0';
        keyBStr += String(keyB[i], HEX);
    }
    keyBStr.toUpperCase();

    // Inietta le chiavi nel dump e legge i blocchi per ottenere il credito
    microelInjectKeys(g_dump);
    showInfo(
        "Microel Info",
        "UID: " + uidHex +
            "\n"
            "KeyA: " +
            keyAStr + "\nKeyB: " + keyBStr +
            "\n"
            "Lettura credito..."
    );

    uint8_t sectorsRead = 0;
    mifareReadDump(sectorsRead); // legge i blocchi con le chiavi KDF

    // Prepara la stringa del credito corrente
    String creditStr = "unknow"; // valore di fallback se il blocco non è stato letto
    if (g_dump.blockRead[MICROEL_CREDIT_BLOCK]) {
        uint16_t c = microelGetCredit(g_dump);
        // Formatta come "X.XX EUR" con padding dello zero per i centesimi < 10
        creditStr = String(c / 100) + "." + (c % 100 < 10 ? "0" : "") + String(c % 100) + " EUR";
    }

    // Prepara la stringa del credito precedente
    String prevStr = "unknow";
    if (g_dump.blockRead[MICROEL_PREV_CREDIT_BLOCK]) {
        const uint8_t *b = g_dump.data[MICROEL_PREV_CREDIT_BLOCK];
        uint16_t p = (uint16_t)(b[MICROEL_CREDIT_BYTE_LOW] | (b[MICROEL_CREDIT_BYTE_HIGH] << 8));
        prevStr = String(p / 100) + "." + (p % 100 < 10 ? "0" : "") + String(p % 100) + " EUR";
    }

    // Cerca il gestore associato all'UID in /rfid/gestori_map.txt
    String gestore = lookupGestore(uidHex);
    if (gestore.isEmpty()) gestore = "unknow"; // nessun gestore associato

    // Mostra tutte le informazioni in un unico pannello e attende un tasto
    showMessage(
        "Microel Info",
        "UID: " + uidHex + "\n" + "KeyA: " + keyAStr + "\n" + "KeyB: " + keyBStr + "\n" +
            "Credito: " + creditStr + "\n" + "Credito Precedente: " + prevStr + "\n" + "Gestore: " + gestore
    );
}

// ─── Scrittura con blocco 0 ───────────────────────────────────────────────────

/**
 * @brief Scrive il dump Microel sul tag incluso il blocco 0 (dati produttore).
 *
 * Fase 1 – mifareWriteDump(): scrive tutti i settori con le relative chiavi.
 *   Il blocco 0 è escluso internamente da mifareClassicWriteDump() perché
 *   su tag originali è read-only.
 *
 * Fase 2 – mifareWriteBlock0(): scrive specificamente il blocco 0.
 *   Su tag magic (CUID, FUID, GEN2) questa operazione sovrascrive l'UID,
 *   il BCC e i dati produttore, completando la clonazione.
 *   Su tag originali MIFARE la scrittura viene rifiutata dall'hardware
 *   (mifareWriteBlock0() ritorna false) ma non causa errori bloccanti.
 *
 * Il parametro block0Written permette all'UI di distinguere i due casi
 * e informare l'utente se la clonazione è stata completa o parziale.
 */
bool microelWriteCard(const MifareDump &src, uint8_t &sectorsWritten, bool &block0Written) {
    // Fase 1: scrittura di tutti i blocchi normali (blocco 0 saltato internamente)
    bool ok = mifareWriteDump(src, sectorsWritten);

    // Fase 2: scrittura aggiuntiva del blocco 0 (solo su tag magic)
    block0Written = mifareWriteBlock0(src);

    if (block0Written) Serial.println("[Microel] Blocco 0 scritto (tag magic).");
    else Serial.println("[Microel] Blocco 0 non scritto (tag originale o blocco assente).");

    return ok; // il risultato principale dipende dalla fase 1
}

// ─── Menu ricarica via Serial ─────────────────────────────────────────────────

/**
 * @brief Menu interattivo via Serial Monitor per scegliere l'importo di ricarica.
 *
 * Mostra le opzioni numerate 1–5 più l'opzione 0 per annullare.
 * Attende l'input dell'utente con un timeout di 30 secondi.
 * Se l'input non arriva entro il timeout, restituisce 0 (annullato).
 */
uint16_t microelCreditMenu() {
    Serial.println("\n=== RICARICA MICROEL ===");
    Serial.println("Seleziona il nuovo importo:");

    // Mostra tutte le opzioni disponibili con il loro indice
    for (uint8_t i = 0; i < MICROEL_CREDIT_OPTIONS_COUNT; i++)
        Serial.printf("  %d. %s\n", i + 1, MICROEL_CREDIT_LABELS[i]);

    Serial.println("  0. Annulla");
    Serial.print("\nScelta: ");

    // Attende l'input con timeout di 30 secondi
    unsigned long start = millis();
    while (!Serial.available() && millis() - start < 30000) delay(50);

    if (!Serial.available()) {
        Serial.println("\n[Microel] Timeout: operazione annullata.");
        return 0; // timeout scaduto → annullato
    }

    int choice = Serial.parseInt(); // legge il numero digitato
    Serial.println(choice);         // echo per conferma visiva

    // Valida la scelta: deve essere nell'intervallo 1–N o 0 per annullare
    if (choice < 1 || choice > MICROEL_CREDIT_OPTIONS_COUNT) {
        Serial.println("[Microel] Scelta non valida.");
        return 0;
    }

    uint16_t selected = MICROEL_CREDIT_OPTIONS[choice - 1]; // converti 1-based in 0-based
    Serial.printf("[Microel] Selezionato: %s\n", MICROEL_CREDIT_LABELS[choice - 1]);
    return selected;
}

/**
 * @brief Flusso completo di ricarica: leggi → scegli importo → conferma → scrivi.
 *
 * Step 1: Attende la tessera Microel e legge tutti i settori.
 * Step 2: Stampa su Serial il credito corrente.
 * Step 3: Mostra il menu di selezione importo e attende la scelta.
 * Step 4: Chiede conferma esplicita (s/n) con timeout di 15 secondi.
 * Step 5: Aggiorna il credito in RAM con microelSetCredit().
 * Step 6: Scrive sul tag fisico con microelWriteCard() (include blocco 0).
 *
 * Se uno qualsiasi dei step fallisce o l'utente annulla, la funzione
 * ritorna false senza modificare il tag fisico.
 */
bool microelRechargeTessera() {
    // Step 1: lettura tessera
    uint8_t sectorsRead = 0;
    if (!microelReadCard(sectorsRead)) {
        Serial.println("[Microel] Lettura fallita. Operazione annullata.");
        return false;
    }

    // Step 2: mostra credito attuale su Serial
    microelPrintCreditInfo(g_dump);

    // Step 3: selezione importo tramite menu Serial
    uint16_t newCredit = microelCreditMenu();
    if (newCredit == 0) {
        Serial.println("[Microel] Annullato dall'utente.");
        return false;
    }

    // Step 4: conferma esplicita dell'operazione
    Serial.printf("\nConfermi ricarica a %d.%02d EUR? (s/n): ", newCredit / 100, newCredit % 100);

    unsigned long start = millis();
    while (!Serial.available() && millis() - start < 15000) delay(50);

    if (!Serial.available()) {
        Serial.println("\n[Microel] Timeout conferma. Annullato.");
        return false;
    }

    String confirm = Serial.readStringUntil('\n');
    confirm.trim();
    confirm.toLowerCase();

    // Accetta "s", "si", "y", "yes" come conferma positiva
    if (confirm != "s" && confirm != "si" && confirm != "y" && confirm != "yes") {
        Serial.println("[Microel] Operazione annullata.");
        return false;
    }

    // Step 5: aggiorna credito in RAM (blocco 4 e blocco 5)
    microelSetCredit(g_dump, newCredit);

    // Step 6: scrivi sul tag fisico incluso il blocco 0
    Serial.println("[Microel] Avvicina nuovamente la tessera per scrivere...");
    uint8_t sectorsWritten = 0;
    bool block0Written = false;
    bool ok = microelWriteCard(g_dump, sectorsWritten, block0Written);

    if (!ok) {
        Serial.println("[Microel] Scrittura fallita!");
        return false;
    }

    // Riepilogo finale
    Serial.printf(
        "[Microel] Scrittura OK. Settori: %d. Blocco 0: %s\n",
        sectorsWritten,
        block0Written ? "scritto" : "non scritto"
    );
    Serial.printf("[Microel] Nuovo credito: %d.%02d EUR\n", newCredit / 100, newCredit % 100);
    return true;
}
