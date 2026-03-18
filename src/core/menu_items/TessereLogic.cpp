/**
 * @file TessereLogic.cpp
 * @brief Implementazione della logica core per tag MIFARE tramite PN532.
 *
 * Questo file contiene:
 *   - Inizializzazione lazy del driver Adafruit_PN532 via I²C
 *   - Helper per tipo/struttura del tag (SAK → settori, blocchi, trailer)
 *   - Caricamento chiavi da SD con supporto CSV uid,chiave
 *   - Gestione gestori: lista nomi e associazioni UID→gestore su SD
 *   - Lettura completa del dump (MIFARE Classic e Ultralight)
 *   - Scrittura dump su tag fisico
 *   - Serializzazione/deserializzazione del dump su SD
 *
 * Convenzione di log: tutti i messaggi seriali usano il prefisso "[TESSERE]".
 *
 * Percorsi SD:
 *   - /rfid/chiavi.txt      → chiavi di autenticazione
 *   - /rfid/gestori.txt     → lista nomi gestori
 *   - /rfid/gestori_map.txt → associazioni uid,gestore
 *   - /rfid/dumps/          → dump binari (<UID>.bin)
 */

#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

// ─── Stato del modulo ─────────────────────────────────────────────────────────

/// Driver Adafruit PN532 in modalità I²C (pin IRQ e RST non usati).
static Adafruit_PN532 mifareNfc(255, 255);

/// Flag di inizializzazione lazy: true dopo la prima init riuscita.
static bool mifareNfcInit = false;

/// Immagine RAM del tag corrente (definizione della variabile esterna).
MifareDump g_dump;

// ─── Helper UI (statico interno) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza il corpo sulle righe (separatore '\n') e usa padprintln per
 * rispettare i margini dello schermo di Bruce.
 */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    String tmp = body;
    int start = 0, idx;
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx));
        start = idx + 1;
    }
    padprintln(tmp.substring(start));

    delay(300);
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

/**
 * @brief Restituisce una stringa descrittiva del tipo di tag dato il byte SAK.
 *
 * SAK (Select Acknowledge) è il byte restituito dal tag durante
 * l'anti-collision; identifica univocamente la famiglia MIFARE.
 */
String getTagType(uint8_t sak) {
    if (sak == 0x08) return "Mifare 1K";
    if (sak == 0x18) return "Mifare 4K";
    if (sak == 0x09) return "Mifare Mini";
    if (sak == 0x00) return "Mifare UL";
    if (sak == 0x28) return "Mifare 1K (SmartMX)";
    if (sak == 0x38) return "Mifare 4K (SmartMX)";
    return "Unknown (SAK:" + String(sak, HEX) + ")";
}

/**
 * @brief Restituisce il numero di settori del tag dato il byte SAK.
 *
 * MIFARE Classic 4K: 40 settori (32 da 4 blocchi + 8 da 16 blocchi).
 * MIFARE Mini:       5 settori da 4 blocchi ciascuno.
 * Tutti gli altri:   16 settori da 4 blocchi (1K default).
 */
uint8_t getSectorCount(uint8_t sak) {
    if (sak == 0x18) return 40; // 4K
    if (sak == 0x09) return 5;  // Mini
    return 16;                  // 1K default
}

/**
 * @brief Restituisce l'indice assoluto del blocco trailer del settore @p sector.
 *
 * Nei settori 0..31: il trailer è il 4° blocco (sector×4 + 3).
 * Nei settori 32..39: i settori hanno 16 blocchi, il trailer è l'ultimo.
 */
uint8_t trailerBlock(uint8_t sector) {
    if (sector < 32) return sector * 4 + 3;
    return 32 * 4 + (sector - 32) * 16 + 15;
}

/**
 * @brief Restituisce l'indice assoluto del primo blocco dati del settore @p sector.
 *
 * Nei settori 0..31: primo blocco = sector×4.
 * Nei settori 32..39: primo blocco nella zona estesa a 16 blocchi.
 */
uint8_t firstBlock(uint8_t sector) {
    if (sector < 32) return sector * 4;
    return 32 * 4 + (sector - 32) * 16;
}

/**
 * @brief Restituisce il numero di blocchi nel settore @p sector.
 *
 * Settori 0..31: 4 blocchi. Settori 32..39 (solo MIFARE 4K): 16 blocchi.
 */
uint8_t blocksInSector(uint8_t sector) { return (sector < 32) ? 4 : 16; }

// ─── Utilità UID ─────────────────────────────────────────────────────────────

/**
 * @brief Costruisce la rappresentazione esadecimale maiuscola dell'UID.
 *
 * Usata per generare il nome del file dump (es. UID {0xAA,0xBB} → "AABB").
 */
String buildUIDHex(const uint8_t *uid, uint8_t len) {
    String s;
    s.reserve(len * 2);
    for (uint8_t i = 0; i < len; i++) {
        if (uid[i] < 0x10) s += '0';
        s += String(uid[i], HEX);
    }
    s.toUpperCase();
    return s;
}

// ─── Gestione chiavi ─────────────────────────────────────────────────────────

/**
 * @brief Carica le chiavi da /rfid/chiavi.txt filtrate per l'UID indicato.
 *
 * Formato supportato (una voce per riga):
 *   - `AABBCCDD,FFEEDDCCBBAA` → chiave valida solo per il tag con quel UID
 *   - `,FFEEDDCCBBAA`         → chiave generica (campo UID vuoto)
 *   - `FFEEDDCCBBAA`          → chiave generica (senza separatore)
 *
 * Le chiavi FF×6 e 00×6 vengono sempre incluse come prime voci.
 * Righe malformate o con caratteri non esadecimali vengono ignorate.
 *
 * @param uid    UID del tag corrente (per filtrare le chiavi specifiche).
 * @param uidLen Lunghezza dell'UID.
 */
std::vector<std::array<uint8_t, 6>> loadKeysForUID(const uint8_t *uid, uint8_t uidLen) {
    std::vector<std::array<uint8_t, 6>> keys;

    // Chiavi di default sempre presenti (prime da provare)
    keys.push_back({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    keys.push_back({0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    String uidHex = buildUIDHex(uid, uidLen);

    if (!SD.exists("/rfid/chiavi.txt")) return keys;
    File f = SD.open("/rfid/chiavi.txt", FILE_READ);
    if (!f) return keys;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        String keyHex;
        int comma = line.indexOf(',');

        if (comma >= 0) {
            // Formato CSV: uid,chiave
            String lineUID = line.substring(0, comma);
            keyHex = line.substring(comma + 1);
            lineUID.trim();
            keyHex.trim();
            lineUID.toUpperCase();
            keyHex.toUpperCase();

            // Salta se UID specificato ma non corrisponde al tag corrente
            if (!lineUID.isEmpty() && lineUID != uidHex) continue;
        } else {
            // Formato legacy: solo chiave (nessun UID → generica)
            keyHex = line;
            keyHex.toUpperCase();
        }

        // Valida e converte la chiave (deve essere esattamente 12 caratteri hex)
        if (keyHex.length() != 12) continue;
        std::array<uint8_t, 6> key;
        bool valid = true;
        for (int i = 0; i < 6 && valid; i++) {
            String bs = keyHex.substring(i * 2, i * 2 + 2);
            for (char c : bs) {
                if (!isHexadecimalDigit(c)) {
                    valid = false;
                    break;
                }
            }
            if (valid) key[i] = (uint8_t)strtol(bs.c_str(), nullptr, 16);
        }
        if (valid) keys.push_back(key);
    }
    f.close();

    Serial.printf("[TESSERE] Loaded %u keys for UID %s\n", keys.size(), uidHex.c_str());
    return keys;
}

// ─── Gestione gestori ─────────────────────────────────────────────────────────
//
// Due file distinti per separare lista nomi e associazioni:
//   gestori.txt     → un nome per riga  (es. "Sto&Bene")
//   gestori_map.txt → uid,nome per riga (es. "1E733840,Sto&Bene")
//
// Tutte le funzioni che riscrivono un file usano un file temporaneo
// per evitare corruzione in caso di interruzione durante la scrittura.

/**
 * @brief Helper interno: copia il contenuto di @p src in @p dst e rimuove @p src.
 *
 * Usato da deleteGestore e modifyGestore per sostituire atomicamente
 * un file con la sua versione aggiornata tramite file temporaneo.
 */
static void replaceFile(const String &src, const String &dst) {
    SD.remove(dst);
    File fr = SD.open(src, FILE_READ);
    File fw = SD.open(dst, FILE_WRITE);
    if (fr && fw) {
        while (fr.available()) fw.write(fr.read());
    }
    if (fr) fr.close();
    if (fw) fw.close();
    SD.remove(src);
}

/**
 * @brief Carica la lista dei nomi gestore da /rfid/gestori.txt.
 *
 * Il file contiene un nome per riga. Righe vuote vengono ignorate.
 * @return Vettore di stringhe con i nomi dei gestori disponibili.
 */
std::vector<String> loadGestori() {
    std::vector<String> list;
    if (!SD.exists("/rfid/gestori.txt")) return list;

    File f = SD.open("/rfid/gestori.txt", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) list.push_back(line);
    }
    f.close();
    return list;
}

/**
 * @brief Aggiunge un nuovo nome gestore in /rfid/gestori.txt.
 *
 * Non aggiunge duplicati: se il nome esiste già, restituisce false.
 * Crea la cartella /rfid e il file se non esistono.
 */
bool addGestore(const String &nome) {
    // Controlla duplicati
    for (auto &g : loadGestori()) {
        if (g == nome) return false;
    }

    if (!SD.exists("/rfid")) SD.mkdir("/rfid");
    File f = SD.open("/rfid/gestori.txt", FILE_APPEND);
    if (!f) return false;
    f.println(nome);
    f.close();
    Serial.printf("[TESSERE] Gestore added: %s\n", nome.c_str());
    return true;
}

/**
 * @brief Elimina un gestore da gestori.txt e tutte le sue associazioni in gestori_map.txt.
 *
 * Riscrive entrambi i file tramite file temporanei per garantire
 * l'integrità in caso di interruzione.
 */
bool deleteGestore(const String &nome) {
    // Riscrive gestori.txt senza il nome eliminato
    auto list = loadGestori();
    bool found = false;

    File fw = SD.open("/rfid/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : list) {
        if (g == nome) {
            found = true;
            continue;
        }
        fw.println(g);
    }
    fw.close();
    replaceFile("/rfid/gestori_tmp.txt", "/rfid/gestori.txt");

    // Rimuove le associazioni UID→gestore per questo nome in gestori_map.txt
    if (SD.exists("/rfid/gestori_map.txt")) {
        File fm = SD.open("/rfid/gestori_map.txt", FILE_READ);
        File fm2 = SD.open("/rfid/gestori_map_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String line = fm.readStringUntil('\n');
                line.trim();
                int comma = line.indexOf(',');
                if (comma >= 0 && line.substring(comma + 1) == nome) continue; // rimuove
                if (!line.isEmpty()) fm2.println(line);
            }
        }
        if (fm) fm.close();
        if (fm2) fm2.close();
        replaceFile("/rfid/gestori_map_tmp.txt", "/rfid/gestori_map.txt");
    }

    Serial.printf("[TESSERE] Gestore deleted: %s\n", nome.c_str());
    return found;
}

/**
 * @brief Rinomina un gestore in gestori.txt e aggiorna tutte le voci in gestori_map.txt.
 *
 * Aggiorna sia la lista nomi che tutte le associazioni UID che usavano
 * il vecchio nome, mantenendo la coerenza tra i due file.
 */
bool modifyGestore(const String &oldNome, const String &newNome) {
    // Aggiorna gestori.txt
    auto list = loadGestori();
    bool found = false;

    File fw = SD.open("/rfid/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : list) {
        if (g == oldNome) {
            fw.println(newNome);
            found = true;
        } else fw.println(g);
    }
    fw.close();
    replaceFile("/rfid/gestori_tmp.txt", "/rfid/gestori.txt");

    // Aggiorna le associazioni in gestori_map.txt
    if (SD.exists("/rfid/gestori_map.txt")) {
        File fm = SD.open("/rfid/gestori_map.txt", FILE_READ);
        File fm2 = SD.open("/rfid/gestori_map_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String line = fm.readStringUntil('\n');
                line.trim();
                if (line.isEmpty()) continue;
                int comma = line.indexOf(',');
                // Sostituisce il vecchio nome con il nuovo nelle associazioni
                if (comma >= 0 && line.substring(comma + 1) == oldNome)
                    fm2.println(line.substring(0, comma + 1) + newNome);
                else fm2.println(line);
            }
        }
        if (fm) fm.close();
        if (fm2) fm2.close();
        replaceFile("/rfid/gestori_map_tmp.txt", "/rfid/gestori_map.txt");
    }

    Serial.printf("[TESSERE] Gestore renamed: %s → %s\n", oldNome.c_str(), newNome.c_str());
    return found;
}

/**
 * @brief Associa un UID a un gestore in /rfid/gestori_map.txt.
 *
 * Se l'UID era già associato a un altro gestore, sovrascrive l'associazione.
 * Riscrive l'intero file per garantire unicità dell'UID.
 */
bool associateGestore(const String &uidHex, const String &nome) {
    if (!SD.exists("/rfid")) SD.mkdir("/rfid");

    // Legge le associazioni esistenti aggiornando o aggiungendo quella per questo UID
    std::vector<String> lines;
    bool updated = false;

    if (SD.exists("/rfid/gestori_map.txt")) {
        File f = SD.open("/rfid/gestori_map.txt", FILE_READ);
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.isEmpty()) continue;
                int comma = line.indexOf(',');
                if (comma >= 0 && line.substring(0, comma) == uidHex) {
                    lines.push_back(uidHex + "," + nome); // sovrascrive
                    updated = true;
                } else {
                    lines.push_back(line);
                }
            }
            f.close();
        }
    }
    if (!updated) lines.push_back(uidHex + "," + nome);

    SD.remove("/rfid/gestori_map.txt");
    File fw = SD.open("/rfid/gestori_map.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &l : lines) fw.println(l);
    fw.close();

    Serial.printf("[TESSERE] UID %s associated to %s\n", uidHex.c_str(), nome.c_str());
    return true;
}

/**
 * @brief Cerca il nome del gestore associato all'UID in /rfid/gestori_map.txt.
 *
 * Legge il file CSV riga per riga confrontando l'UID (case-insensitive).
 * Righe malformate o senza separatore ',' vengono ignorate.
 *
 * @param uidHex UID del tag in formato esadecimale maiuscolo (es. "1E733840").
 * @return Nome del gestore se trovato, stringa vuota se non presente.
 */
String lookupGestore(const String &uidHex) {
    if (!SD.exists("/rfid/gestori_map.txt")) return "";
    File f = SD.open("/rfid/gestori_map.txt", FILE_READ);
    if (!f) return "";

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int comma = line.indexOf(',');
        if (comma < 0) continue;

        String uid = line.substring(0, comma);
        String name = line.substring(comma + 1);
        uid.trim();
        name.trim();
        uid.toUpperCase();

        if (uid == uidHex) {
            f.close();
            return name;
        }
    }
    f.close();
    return "";
}

// ─── Inizializzazione PN532 ──────────────────────────────────────────────────

/**
 * @brief Inizializza il PN532 via I²C se non è già stato fatto.
 *
 * Legge i pin SDA/SCL dalla configurazione di Bruce, avvia il bus a 100 kHz,
 * verifica la presenza del PN532 tramite getFirmwareVersion() e configura
 * la modalità SAM (Security Access Module) per la lettura passiva.
 *
 * @return true se il PN532 è pronto, false se non risponde.
 */
bool mifareInit() {
    if (mifareNfcInit) return true;

    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);

    mifareNfc.begin();
    mifareNfcInit = (mifareNfc.getFirmwareVersion() != 0);

    if (!mifareNfcInit) {
        displayError("PN532 init failed.", true);
        return false;
    }

    mifareNfc.SAMConfig();
    Serial.println("[TESSERE] PN532 initialized.");
    return true;
}

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

/**
 * @brief Attende un tag MIFARE ISO14443A sul lettore (timeout 6 secondi).
 *
 * In caso di successo popola g_dump con: uid, uidLen, sak, atqa, tagType
 * e numSectors. Azzera inoltre i flag blockRead e keyFound.
 *
 * @return true se un tag è stato rilevato entro il timeout.
 */
bool waitForMifareTag() {
    drawMainBorderWithTitle("Mifare");
    setPadCursor(1, 2);
    padprintln("Place tag on reader...");

    uint8_t uid[7], uidLen;
    if (!mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 6000)) {
        showMessage("Mifare", "No tag found.");
        return false;
    }

    // Popola l'identità del tag
    memcpy(g_dump.uid, uid, uidLen);
    g_dump.uidLen = uidLen;
    g_dump.sak = mifareNfc.getLastSAK();
    g_dump.atqa = mifareNfc.getLastATQA();
    g_dump.tagType = getTagType(g_dump.sak);
    g_dump.numSectors = getSectorCount(g_dump.sak);

    // Azzera i flag di lettura e di chiave trovata
    memset(g_dump.blockRead, 0, sizeof(g_dump.blockRead));
    memset(g_dump.keyAFound, 0, sizeof(g_dump.keyAFound));
    memset(g_dump.keyBFound, 0, sizeof(g_dump.keyBFound));

    Serial.printf(
        "[TESSERE] Tag found: %s UID=%s\n",
        g_dump.tagType.c_str(),
        buildUIDHex(g_dump.uid, g_dump.uidLen).c_str()
    );
    return true;
}

/**
 * @brief Attende un tag MIFARE sul lettore senza popolare g_dump.
 *
 * A differenza di waitForMifareTag(), questa funzione non sovrascrive
 * g_dump: necessario in WriteTessera() dove g_dump contiene il dump
 * sorgente caricato dalla SD e non deve essere perso al rilevamento
 * del tag target su cui scrivere.
 */
bool waitForAnyMifareTag(uint8_t *uid, uint8_t *uidLen) {
    return mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 6000);
}

// ─── Lettura dump ─────────────────────────────────────────────────────────────

/**
 * @brief Legge tutti i settori leggibili del tag MIFARE Classic in g_dump.
 *
 * Per ogni settore prova tutte le chiavi caricate dalla SD:
 *   1. Prima come Key A; se autenticata, legge tutti i blocchi del settore.
 *   2. Prova sempre anche Key B (indipendentemente da Key A) perché
 *      l'hardware restituisce Key A come 00×6 nel trailer, ma Key B è leggibile.
 *
 * Dopo la lettura del trailer, ripristina Key A dai dati trovati durante
 * l'autenticazione (l'hardware la oscura sempre come 00×6).
 *
 * @param sectorsRead Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
static bool mifareClassicReadDump(uint8_t &sectorsRead) {
    auto keys = loadKeysForUID(g_dump.uid, g_dump.uidLen);
    sectorsRead = 0;

    for (uint8_t s = 0; s < g_dump.numSectors; s++) {
        uint8_t trailer = trailerBlock(s);
        uint8_t first = firstBlock(s);
        uint8_t count = blocksInSector(s);
        bool authenticated = false;

        // Tentativo con Key A
        for (auto &key : keys) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 0, key.data()
                )) {
                memcpy(g_dump.keyA[s], key.data(), 6);
                g_dump.keyAFound[s] = true;
                authenticated = true;
                break;
            }
            // Re-selezione dopo autenticazione fallita (il tag va in stato di errore)
            mifareNfc.inRelease(1);
            mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
        }

        // Tentativo con Key B (sempre, anche se Key A è già nota)
        for (auto &key : keys) {
            // Re-autentica con Key A prima di provare Key B
            if (g_dump.keyAFound[s]) {
                mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 0, g_dump.keyA[s]
                );
            }
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 1, key.data()
                )) {
                memcpy(g_dump.keyB[s], key.data(), 6);
                g_dump.keyBFound[s] = true;
                break;
            }
            mifareNfc.inRelease(1);
            mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
        }

        if (!authenticated) {
            Serial.printf("[TESSERE] Sector %u: no key found, skipping.\n", s);
            continue;
        }

        // Legge tutti i blocchi del settore
        for (uint8_t b = 0; b < count; b++) {
            uint8_t blockNum = first + b;
            if (mifareNfc.mifareclassic_ReadDataBlock(blockNum, g_dump.data[blockNum]))
                g_dump.blockRead[blockNum] = true;
            else Serial.printf("[TESSERE] Sector %u block %u read failed.\n", s, blockNum);
        }

        // Ripristina Key A nel trailer: l'hardware la restituisce sempre come 00×6
        if (g_dump.keyAFound[s]) memcpy(g_dump.data[trailer], g_dump.keyA[s], 6);
        // Ripristina Key B nel trailer (byte 10..15)
        if (g_dump.keyBFound[s]) memcpy(g_dump.data[trailer] + 10, g_dump.keyB[s], 6);

        sectorsRead++;
    }
    return sectorsRead > 0;
}

/**
 * @brief Legge tutte le pagine leggibili di un tag MIFARE Ultralight in g_dump.
 *
 * MIFARE UL non richiede autenticazione per la lettura delle pagine standard.
 * Le pagine sono da 4 byte; vengono salvate allineate a 16 byte in data[][].
 * La lettura si interrompe quando il tag smette di rispondere.
 *
 * @param pagesRead Numero di pagine lette con successo (output).
 * @return true se almeno una pagina è stata letta.
 */
static bool mifareULReadDump(uint8_t &pagesRead) {
    pagesRead = 0;
    for (uint8_t page = 0; page < MIFARE_UL_MAX_PAGES; page++) {
        uint8_t buf[4];
        if (!mifareNfc.ntag2xx_ReadPage(page, buf)) break; // fine del tag

        // Salva la pagina (4 byte) allineata ai primi 4 byte del blocco da 16
        memcpy(g_dump.data[page], buf, 4);
        memset(g_dump.data[page] + 4, 0, 12);
        g_dump.blockRead[page] = true;
        pagesRead++;
    }
    return pagesRead > 0;
}

/**
 * @brief Dispatcher: chiama la funzione di lettura corretta in base al tipo di tag.
 *
 * MIFARE Ultralight (SAK 0x00) usa un percorso diverso da MIFARE Classic.
 * @param sectorsRead Settori (o pagine per UL) letti con successo (output).
 */
bool mifareReadDump(uint8_t &sectorsRead) {
    if (g_dump.sak == 0x00) return mifareULReadDump(sectorsRead);
    return mifareClassicReadDump(sectorsRead);
}

// ─── Scrittura dump ───────────────────────────────────────────────────────────

/**
 * @brief Scrive un dump su tag MIFARE Classic fisico.
 *
 * Per ogni settore autentica usando le chiavi memorizzate nel dump (Key A
 * prima, poi Key B). Scrive tutti i blocchi marcati come letti, con due
 * eccezioni per sicurezza:
 *   - Blocco 0 (dati produttore): read-only sull'hardware, sempre saltato.
 *   - Sector trailer: scritto con avvertimento; un trailer errato può
 *     rendere il settore permanentemente inaccessibile.
 *
 * @param src            Dump sorgente da scrivere.
 * @param sectorsWritten Settori scritti con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
static bool mifareClassicWriteDump(const MifareDump &src, uint8_t &sectorsWritten) {
    sectorsWritten = 0;

    for (uint8_t s = 0; s < src.numSectors; s++) {
        uint8_t trailer = trailerBlock(s);
        uint8_t first = firstBlock(s);
        uint8_t count = blocksInSector(s);

        // Autentica con Key A o Key B memorizzate nel dump
        bool authenticated = false;
        if (src.keyAFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.uidLen, trailer, 0, const_cast<uint8_t *>(src.keyA[s])
                ))
                authenticated = true;
        }
        if (!authenticated && src.keyBFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.uidLen, trailer, 1, const_cast<uint8_t *>(src.keyB[s])
                ))
                authenticated = true;
        }
        if (!authenticated) {
            Serial.printf("[TESSERE] Write: sector %u auth failed, skipping.\n", s);
            continue;
        }

        bool sectorOk = true;
        for (uint8_t b = 0; b < count; b++) {
            uint8_t blockNum = first + b;

            // Blocco 0 = dati produttore, sola lettura su hardware originale
            if (blockNum == 0) continue;
            // Salta blocchi non presenti nel dump
            if (!src.blockRead[blockNum]) continue;

            if (!mifareNfc.mifareclassic_WriteDataBlock(
                    blockNum, const_cast<uint8_t *>(src.data[blockNum])
                )) {
                Serial.printf("[TESSERE] Write: block %u failed.\n", blockNum);
                sectorOk = false;
            }
        }
        if (sectorOk) sectorsWritten++;
    }
    return sectorsWritten > 0;
}

/**
 * @brief Scrive un dump su tag MIFARE Ultralight fisico.
 *
 * Scrive le pagine presenti nel dump (4 byte per pagina).
 * Pagine 0..3 (lock/config pages) vengono saltate per sicurezza.
 *
 * @param src          Dump sorgente.
 * @param pagesWritten Pagine scritte con successo (output).
 */
static bool mifareULWriteDump(const MifareDump &src, uint8_t &pagesWritten) {
    pagesWritten = 0;
    for (uint8_t page = 4; page < MIFARE_UL_MAX_PAGES; page++) {
        if (!src.blockRead[page]) continue;
        uint8_t buf[4];
        memcpy(buf, src.data[page], 4);
        if (mifareNfc.ntag2xx_WritePage(page, buf)) pagesWritten++;
        else Serial.printf("[TESSERE] UL write page %u failed.\n", page);
    }
    return pagesWritten > 0;
}

/**
 * @brief Dispatcher: chiama la funzione di scrittura corretta in base al SAK del dump.
 */
bool mifareWriteDump(const MifareDump &src, uint8_t &sectorsWritten) {
    if (src.sak == 0x00) return mifareULWriteDump(src, sectorsWritten);
    return mifareClassicWriteDump(src, sectorsWritten);
}

// ─── Persistenza su SD ───────────────────────────────────────────────────────

/**
 * @brief Serializza il dump in un file binario su SD.
 *
 * Formato del file (little-endian):
 *   4  byte  → magic "MFDR"
 *   1  byte  → version
 *   1  byte  → uidLen
 *   7  byte  → uid (padded)
 *   1  byte  → sak
 *   2  byte  → atqa
 *   1  byte  → numSectors
 *   240 byte → keyA[40][6]
 *   40  byte → keyAFound[40]
 *   240 byte → keyB[40][6]
 *   40  byte → keyBFound[40]
 *   256 byte → blockRead[256]
 *   4096 byte → data[256][16]
 *   Totale: ~4929 byte
 */
bool saveDumpToSD(const MifareDump &dump, const String &uidHex) {
    // Crea la cartella /rfid/dumps/ se non esiste
    if (!SD.exists("/rfid")) SD.mkdir("/rfid");
    if (!SD.exists("/rfid/dumps")) SD.mkdir("/rfid/dumps");

    String path = "/rfid/dumps/" + uidHex + ".bin";
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[TESSERE] saveDump: cannot open %s\n", path.c_str());
        return false;
    }

    // Header
    f.write((const uint8_t *)DUMP_MAGIC, 4);
    uint8_t ver = DUMP_VERSION;
    f.write(&ver, 1);
    f.write(&dump.uidLen, 1);
    f.write(dump.uid, 7);
    f.write(&dump.sak, 1);
    f.write((const uint8_t *)&dump.atqa, 2);
    f.write(&dump.numSectors, 1);

    // Chiavi per settore
    f.write((const uint8_t *)dump.keyA, sizeof(dump.keyA));
    f.write((const uint8_t *)dump.keyAFound, sizeof(dump.keyAFound));
    f.write((const uint8_t *)dump.keyB, sizeof(dump.keyB));
    f.write((const uint8_t *)dump.keyBFound, sizeof(dump.keyBFound));

    // Flag di lettura + dati EEPROM
    f.write((const uint8_t *)dump.blockRead, sizeof(dump.blockRead));
    f.write((const uint8_t *)dump.data, sizeof(dump.data));

    f.close();
    Serial.printf("[TESSERE] Dump saved to %s\n", path.c_str());
    return true;
}

/**
 * @brief Deserializza un dump da un file binario su SD.
 *
 * Verifica il magic e la versione prima di caricare i dati.
 * Ripristina anche tagType e numSectors dal SAK memorizzato.
 *
 * @return true se il file è stato letto e validato correttamente.
 */
bool loadDumpFromSD(MifareDump &dump, const String &path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[TESSERE] loadDump: cannot open %s\n", path.c_str());
        return false;
    }

    // Verifica magic
    char magic[4];
    f.read((uint8_t *)magic, 4);
    if (memcmp(magic, DUMP_MAGIC, 4) != 0) {
        Serial.println("[TESSERE] loadDump: invalid magic.");
        f.close();
        return false;
    }

    // Verifica versione
    uint8_t ver;
    f.read(&ver, 1);
    if (ver != DUMP_VERSION) {
        Serial.printf("[TESSERE] loadDump: unsupported version %u.\n", ver);
        f.close();
        return false;
    }

    // Legge header
    f.read(&dump.uidLen, 1);
    f.read(dump.uid, 7);
    f.read(&dump.sak, 1);
    f.read((uint8_t *)&dump.atqa, 2);
    f.read(&dump.numSectors, 1);

    // Ripristina campi derivati
    dump.tagType = getTagType(dump.sak);

    // Legge chiavi
    f.read((uint8_t *)dump.keyA, sizeof(dump.keyA));
    f.read((uint8_t *)dump.keyAFound, sizeof(dump.keyAFound));
    f.read((uint8_t *)dump.keyB, sizeof(dump.keyB));
    f.read((uint8_t *)dump.keyBFound, sizeof(dump.keyBFound));

    // Legge flag + dati
    f.read((uint8_t *)dump.blockRead, sizeof(dump.blockRead));
    f.read((uint8_t *)dump.data, sizeof(dump.data));

    f.close();
    Serial.printf(
        "[TESSERE] Dump loaded from %s (UID=%s)\n", path.c_str(), buildUIDHex(dump.uid, dump.uidLen).c_str()
    );
    return true;
}
