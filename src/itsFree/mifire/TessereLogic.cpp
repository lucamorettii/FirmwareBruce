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
 *   - Scrittura dump su tag fisico (senza e con blocco 0)
 *   - Serializzazione/deserializzazione del dump su SD
 *
 * Convenzione di log: tutti i messaggi seriali usano il prefisso "[TESSERE]".
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunta implementazione di mifareWriteBlock0() per la scrittura del
 *     blocco 0 su tag magic (necessaria per la clonazione completa Microel).
 */

#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

// ─── Stato del modulo ─────────────────────────────────────────────────────────

/// Driver PN532 in modalità I²C (pin IRQ e RST non usati, quindi 255).
static Adafruit_PN532 mifareNfc(255, 255);

/// Flag di inizializzazione lazy: diventa true dopo la prima init riuscita.
/// Evita di reinizializzare l'I²C e il PN532 a ogni operazione.
static bool mifareNfcInit = false;

/// Definizione della variabile globale g_dump dichiarata extern in TessereLogic.h.
/// Contiene l'immagine RAM del tag attualmente in lavorazione.
MifareDump g_dump;

// ─── Helper UI (statico interno) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza il corpo sulle righe (separatore '\n') e usa padprintln per
 * rispettare i margini dello schermo di Bruce. Attende minimo 300 ms
 * per evitare letture accidentali di tasti rimasti premuti.
 *
 * @param title Titolo del pannello (mostrato nella barra superiore).
 * @param body  Testo da mostrare, separato da '\n' per le righe multiple.
 */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    String tmp = body;
    int start = 0, idx;

    // Scorre il testo riga per riga e lo stampa
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx));
        start = idx + 1;
    }
    padprintln(tmp.substring(start)); // stampa l'ultima riga (senza '\n' finale)

    delay(300); // pausa minima per evitare lettura immediata del tasto

    // Attende la pressione di qualsiasi tasto prima di tornare
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

/**
 * @brief Restituisce una stringa descrittiva del tipo di tag dato il byte SAK.
 *
 * Il SAK (Select Acknowledge) è il byte restituito dal tag durante
 * l'anti-collision ISO 14443-3 e identifica univocamente la famiglia MIFARE.
 *
 * @param sak Byte SAK letto dal tag.
 * @return Stringa leggibile del tipo tag (es. "Mifare 1K").
 */
String getTagType(uint8_t sak) {
    if (sak == 0x08) return "Mifare 1K";
    if (sak == 0x18) return "Mifare 4K";
    if (sak == 0x09) return "Mifare Mini";
    if (sak == 0x00) return "Mifare UL";
    if (sak == 0x28) return "Mifare 1K (SmartMX)";
    if (sak == 0x38) return "Mifare 4K (SmartMX)";
    // SAK sconosciuto: restituisce il valore grezzo per il debug
    return "Unknown (SAK:" + String(sak, HEX) + ")";
}

/**
 * @brief Restituisce il numero di settori del tag dato il byte SAK.
 *
 * MIFARE Classic 4K: 40 settori (32 da 4 blocchi + 8 da 16 blocchi).
 * MIFARE Mini:        5 settori da 4 blocchi ciascuno.
 * Tutti gli altri:   16 settori da 4 blocchi (1K default).
 *
 * @param sak Byte SAK del tag.
 * @return Numero di settori.
 */
uint8_t getSectorCount(uint8_t sak) {
    if (sak == 0x18) return 40; // 4K: 32 settori normali + 8 estesi
    if (sak == 0x09) return 5;  // Mini: 5 settori da 4 blocchi
    return 16;                  // 1K e altri: 16 settori da 4 blocchi
}

/**
 * @brief Restituisce l'indice assoluto del blocco trailer del settore.
 *
 * Il sector trailer è l'ultimo blocco di ogni settore e contiene
 * Key A, Access Bits e Key B. Non deve essere sovrascritto con dati casuali.
 *
 * Settori  0–31: indice = sector × 4 + 3   (es. settore 0 → blocco 3)
 * Settori 32–39: indice nella zona a 16 blocchi del 4K
 *
 * @param sector Indice del settore (0-based).
 * @return Indice assoluto del blocco trailer.
 */
uint8_t trailerBlock(uint8_t sector) {
    if (sector < 32) return sector * 4 + 3;  // zona normale (4 blocchi/settore)
    return 32 * 4 + (sector - 32) * 16 + 15; // zona estesa 4K (16 blocchi/settore)
}

/**
 * @brief Restituisce l'indice assoluto del primo blocco dati del settore.
 *
 * @param sector Indice del settore (0-based).
 * @return Indice assoluto del primo blocco del settore.
 */
uint8_t firstBlock(uint8_t sector) {
    if (sector < 32) return sector * 4; // zona normale
    return 32 * 4 + (sector - 32) * 16; // zona estesa 4K
}

/**
 * @brief Restituisce il numero di blocchi nel settore specificato.
 *
 * @param sector Indice del settore (0-based).
 * @return 4 per i settori 0–31, 16 per i settori 32–39 (solo MIFARE 4K).
 */
uint8_t blocksInSector(uint8_t sector) {
    return (sector < 32) ? 4 : 16; // 4 blocchi nei settori normali, 16 nei settori estesi 4K
}

// ─── Utilità UID ─────────────────────────────────────────────────────────────

/**
 * @brief Costruisce la rappresentazione esadecimale maiuscola dell'UID.
 *
 * Usata per generare il nome del file dump (es. {0xAA,0xBB} → "AABB")
 * e per le ricerche nei file CSV su SD (gestori_map.txt, chiavi.txt).
 *
 * @param uid Puntatore all'array dei byte dell'UID.
 * @param len Numero di byte dell'UID da convertire.
 * @return Stringa esadecimale maiuscola (es. "AABBCCDD").
 */
String buildUIDHex(const uint8_t *uid, uint8_t len) {
    String s;
    s.reserve(len * 2); // pre-alloca per evitare riallocazioni

    for (uint8_t i = 0; i < len; i++) {
        if (uid[i] < 0x10) s += '0'; // padding con zero per byte < 0x10
        s += String(uid[i], HEX);
    }
    s.toUpperCase(); // normalizza a maiuscolo per i confronti
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
 * @param uidLen Lunghezza dell'UID in byte.
 * @return Vettore di chiavi (6 byte ciascuna) da provare per l'autenticazione.
 */
std::vector<std::array<uint8_t, 6>> loadKeysForUID(const uint8_t *uid, uint8_t uidLen) {
    std::vector<std::array<uint8_t, 6>> keys;

    // Chiavi di default sempre presenti e provate per prime
    keys.push_back({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}); // chiave di fabbrica FF
    keys.push_back({0x00, 0x00, 0x00, 0x00, 0x00, 0x00}); // chiave di fabbrica 00

    String uidHex = buildUIDHex(uid, uidLen);

    // Se il file delle chiavi non esiste, restituisce solo le default
    if (!SD.exists("/rfid/chiavi.txt")) return keys;
    File f = SD.open("/rfid/chiavi.txt", FILE_READ);
    if (!f) return keys;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();                  // rimuove spazi e \r
        if (line.isEmpty()) continue; // salta righe vuote

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

            // Salta se l'UID specificato non corrisponde al tag corrente
            if (!lineUID.isEmpty() && lineUID != uidHex) continue;
        } else {
            // Formato legacy: solo la chiave (valida per qualsiasi tag)
            keyHex = line;
            keyHex.toUpperCase();
        }

        // Valida la chiave: deve essere esattamente 12 caratteri esadecimali (6 byte)
        if (keyHex.length() != 12) continue;

        std::array<uint8_t, 6> key;
        bool valid = true;

        for (int i = 0; i < 6 && valid; i++) {
            String bs = keyHex.substring(i * 2, i * 2 + 2); // estrae 2 caratteri per byte
            for (char c : bs) {
                if (!isHexadecimalDigit(c)) {
                    valid = false;
                    break;
                } // carattere non hex
            }
            if (valid) key[i] = (uint8_t)strtol(bs.c_str(), nullptr, 16); // converte hex→byte
        }

        if (valid) keys.push_back(key); // aggiunge solo se la chiave è valida
    }
    f.close();

    Serial.printf("[TESSERE] Loaded %u keys for UID %s\n", keys.size(), uidHex.c_str());
    return keys;
}

// ─── Gestione gestori ─────────────────────────────────────────────────────────

/**
 * @brief Helper interno: sostituisce atomicamente @p dst con il contenuto di @p src.
 *
 * Implementa una sostituzione sicura tramite file temporaneo: copia il contenuto
 * di src in dst e poi elimina src. Usato da deleteGestore() e modifyGestore()
 * per evitare corruzione dei file in caso di interruzione durante la scrittura.
 *
 * @param src Percorso del file temporaneo sorgente.
 * @param dst Percorso del file destinazione da sovrascrivere.
 */
static void replaceFile(const String &src, const String &dst) {
    SD.remove(dst); // rimuove il vecchio file destinazione

    File fr = SD.open(src, FILE_READ);  // apre il temporaneo in lettura
    File fw = SD.open(dst, FILE_WRITE); // crea il nuovo file destinazione

    if (fr && fw) {
        while (fr.available()) fw.write(fr.read()); // copia byte per byte
    }

    if (fr) fr.close();
    if (fw) fw.close();

    SD.remove(src); // rimuove il file temporaneo
}

/**
 * @brief Carica la lista dei nomi gestore da /rfid/gestori.txt.
 * @return Vettore di stringhe con i nomi, vuoto se il file non esiste.
 */
std::vector<String> loadGestori() {
    std::vector<String> list;
    if (!SD.exists("/rfid/gestori.txt")) return list;

    File f = SD.open("/rfid/gestori.txt", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) list.push_back(line); // ignora righe vuote
    }
    f.close();
    return list;
}

/**
 * @brief Aggiunge un nuovo nome gestore in /rfid/gestori.txt.
 *
 * Controlla prima i duplicati caricando la lista completa.
 * Crea la cartella /rfid e il file se non esistono.
 *
 * @param nome Nome del gestore da aggiungere.
 * @return true se aggiunto, false se già esistente o errore SD.
 */
bool addGestore(const String &nome) {
    // Controlla duplicati prima di scrivere
    for (auto &g : loadGestori()) {
        if (g == nome) return false; // gestore già presente
    }

    if (!SD.exists("/rfid")) SD.mkdir("/rfid"); // crea la cartella se necessario

    File f = SD.open("/rfid/gestori.txt", FILE_APPEND); // aggiunge in coda
    if (!f) return false;
    f.println(nome);
    f.close();

    Serial.printf("[TESSERE] Gestore added: %s\n", nome.c_str());
    return true;
}

/**
 * @brief Elimina un gestore da gestori.txt e le sue associazioni da gestori_map.txt.
 *
 * Riscrive entrambi i file tramite file temporanei per garantire integrità.
 * Prima riscrive gestori.txt escludendo il nome, poi aggiorna gestori_map.txt
 * rimuovendo tutte le righe con quel nome come valore.
 *
 * @param nome Nome del gestore da eliminare.
 * @return true se trovato ed eliminato, false se non trovato.
 */
bool deleteGestore(const String &nome) {
    auto list = loadGestori();
    bool found = false;

    // Riscrive gestori.txt saltando il nome da eliminare
    File fw = SD.open("/rfid/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : list) {
        if (g == nome) {
            found = true;
            continue;
        } // salta il gestore da eliminare
        fw.println(g);
    }
    fw.close();
    replaceFile("/rfid/gestori_tmp.txt", "/rfid/gestori.txt");

    // Aggiorna gestori_map.txt rimuovendo le associazioni con questo gestore
    if (SD.exists("/rfid/gestori_map.txt")) {
        File fm = SD.open("/rfid/gestori_map.txt", FILE_READ);
        File fm2 = SD.open("/rfid/gestori_map_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String line = fm.readStringUntil('\n');
                line.trim();
                int comma = line.indexOf(',');
                // Salta la riga se il valore (gestore) corrisponde a quello da eliminare
                if (comma >= 0 && line.substring(comma + 1) == nome) continue;
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
 * @brief Rinomina un gestore in gestori.txt e aggiorna gestori_map.txt.
 *
 * Prima aggiorna gestori.txt sostituendo oldNome con newNome.
 * Poi aggiorna gestori_map.txt: tutte le righe uid,oldNome diventano uid,newNome.
 *
 * @param oldNome Nome attuale da sostituire.
 * @param newNome Nuovo nome da assegnare.
 * @return true se il gestore è stato trovato e rinominato.
 */
bool modifyGestore(const String &oldNome, const String &newNome) {
    auto list = loadGestori();
    bool found = false;

    // Riscrive gestori.txt sostituendo oldNome con newNome
    File fw = SD.open("/rfid/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : list) {
        if (g == oldNome) {
            fw.println(newNome);
            found = true;
        } // sostituisce
        else
            fw.println(g); // mantiene invariato
    }
    fw.close();
    replaceFile("/rfid/gestori_tmp.txt", "/rfid/gestori.txt");

    // Aggiorna gestori_map.txt: sostituisce oldNome con newNome in tutte le associazioni
    if (SD.exists("/rfid/gestori_map.txt")) {
        File fm = SD.open("/rfid/gestori_map.txt", FILE_READ);
        File fm2 = SD.open("/rfid/gestori_map_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String line = fm.readStringUntil('\n');
                line.trim();
                if (line.isEmpty()) continue;
                int comma = line.indexOf(',');
                // Se il gestore corrisponde all'old name, aggiorna con il nuovo nome
                if (comma >= 0 && line.substring(comma + 1) == oldNome)
                    fm2.println(line.substring(0, comma + 1) + newNome);
                else fm2.println(line); // riga non coinvolta: copia invariata
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
 * Riscrive l'intero file per garantire unicità dell'UID come chiave.
 *
 * @param uidHex UID in formato esadecimale maiuscolo (es. "AABBCCDD").
 * @param nome   Nome del gestore da associare.
 * @return true se l'associazione è stata salvata correttamente.
 */
bool associateGestore(const String &uidHex, const String &nome) {
    if (!SD.exists("/rfid")) SD.mkdir("/rfid");

    std::vector<String> lines; // raccoglie tutte le righe aggiornate
    bool updated = false;

    // Legge le associazioni esistenti, aggiornando quella per questo UID se presente
    if (SD.exists("/rfid/gestori_map.txt")) {
        File f = SD.open("/rfid/gestori_map.txt", FILE_READ);
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.isEmpty()) continue;
                int comma = line.indexOf(',');
                if (comma >= 0 && line.substring(0, comma) == uidHex) {
                    lines.push_back(uidHex + "," + nome); // sovrascrive l'associazione esistente
                    updated = true;
                } else {
                    lines.push_back(line); // mantiene le altre associazioni invariate
                }
            }
            f.close();
        }
    }

    // Se l'UID non era presente, aggiunge una nuova riga in fondo
    if (!updated) lines.push_back(uidHex + "," + nome);

    // Riscrive l'intero file con le righe aggiornate
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
 * Righe malformate o senza separatore ',' vengono ignorate silenziosamente.
 *
 * @param uidHex UID in formato esadecimale maiuscolo (es. "1E733840").
 * @return Nome del gestore se trovato, stringa vuota se non presente.
 */
String lookupGestore(const String &uidHex) {
    if (!SD.exists("/rfid/gestori_map.txt")) return ""; // file non esiste
    File f = SD.open("/rfid/gestori_map.txt", FILE_READ);
    if (!f) return "";

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int comma = line.indexOf(',');
        if (comma < 0) continue; // riga malformata: salta

        String uid = line.substring(0, comma);
        String name = line.substring(comma + 1);
        uid.trim();
        name.trim();
        uid.toUpperCase(); // normalizza per confronto case-insensitive

        if (uid == uidHex) {
            f.close();
            return name;
        } // trovato
    }
    f.close();
    return ""; // UID non trovato
}

// ─── Inizializzazione PN532 ──────────────────────────────────────────────────

/**
 * @brief Inizializza il PN532 via I²C se non è già stato fatto (lazy init).
 *
 * Legge i pin SDA/SCL dalla configurazione hardware di Bruce, avvia il bus
 * I²C a 100 kHz, verifica la presenza del PN532 tramite getFirmwareVersion()
 * e configura la modalità SAM (Security Access Module) per la lettura passiva.
 *
 * Dopo la prima inizializzazione riuscita, il flag mifareNfcInit resta true
 * e le chiamate successive ritornano immediatamente senza reinizializzare.
 *
 * @return true se il PN532 è pronto, false se non risponde all'I²C.
 */
bool mifareInit() {
    if (mifareNfcInit) return true; // già inizializzato in sessione precedente

    // Legge i pin I²C dalla configurazione hardware di Bruce
    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;

    Wire.begin(sda_pin, scl_pin); // inizializza il bus I²C con i pin corretti
    Wire.setClock(100000);        // velocità 100 kHz (standard mode, più stabile)

    mifareNfc.begin();

    // Verifica presenza PN532: getFirmwareVersion() ritorna 0 se non trovato
    mifareNfcInit = (mifareNfc.getFirmwareVersion() != 0);

    if (!mifareNfcInit) {
        displayError("PN532 init failed.", true);
        return false;
    }

    mifareNfc.SAMConfig(); // configura SAM per la lettura passiva ISO14443A
    Serial.println("[TESSERE] PN532 initialized.");
    return true;
}

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

/**
 * @brief Attende un tag MIFARE ISO14443A sul lettore (timeout 6 secondi).
 *
 * Mostra a schermo "Place tag on reader..." durante l'attesa.
 * In caso di successo, popola g_dump con: uid, uidLen, sak, atqa, tagType,
 * numSectors. Azzera anche blockRead, keyAFound, keyBFound per pulizia.
 *
 * @return true se un tag è stato rilevato entro il timeout di 6 secondi.
 */
bool waitForMifareTag() {
    // Mostra l'indicazione a schermo mentre attende il tag
    drawMainBorderWithTitle("Mifare");
    setPadCursor(1, 2);
    padprintln("Place tag on reader...");

    uint8_t uid[7], uidLen;

    // Attende il tag con timeout di 6000 ms
    if (!mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 6000)) {
        showMessage("Mifare", "No tag found.");
        return false;
    }

    // Copia i dati di identità del tag in g_dump
    memcpy(g_dump.uid, uid, uidLen);
    g_dump.uidLen = uidLen;
    g_dump.sak = mifareNfc.getLastSAK();            // byte SAK dall'anti-collision
    g_dump.atqa = mifareNfc.getLastATQA();          // word ATQA dall'anti-collision
    g_dump.tagType = getTagType(g_dump.sak);        // stringa leggibile dal SAK
    g_dump.numSectors = getSectorCount(g_dump.sak); // numero settori dal SAK

    // Azzera i flag di stato precedenti per non contaminare la nuova sessione
    memset(g_dump.blockRead, 0, sizeof(g_dump.blockRead));
    memset(g_dump.keyAFound, 0, sizeof(g_dump.keyAFound));
    memset(g_dump.keyBFound, 0, sizeof(g_dump.keyBFound));

    // Azzera anche i buffer delle chiavi
    memset(g_dump.keyA, 0, sizeof(g_dump.keyA));
    memset(g_dump.keyB, 0, sizeof(g_dump.keyB));

    Serial.printf(
        "[TESSERE] Tag found: %s UID=%s\n",
        g_dump.tagType.c_str(),
        buildUIDHex(g_dump.uid, g_dump.uidLen).c_str()
    );
    return true;
}

/**
 * @brief Attende un tag MIFARE sul lettore senza sovrascrivere g_dump.
 *
 * Variante leggera di waitForMifareTag(): esegue solo la lettura passiva
 * e restituisce UID e lunghezza. Non mostra messaggio a schermo e
 * non modifica g_dump, che potrebbe contenere un dump già caricato dalla SD.
 * Usata da WriteTessera() per rilevare il tag target senza perdere il dump sorgente.
 *
 * @param uid    Buffer di almeno 7 byte dove salvare l'UID rilevato.
 * @param uidLen Puntatore dove salvare la lunghezza dell'UID rilevato.
 * @return true se un tag è stato rilevato entro 6 secondi.
 */
bool waitForAnyMifareTag(uint8_t *uid, uint8_t *uidLen) {
    return mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 6000);
}

// ─── Lettura dump ─────────────────────────────────────────────────────────────

/**
 * @brief Legge tutti i settori leggibili del tag MIFARE Classic in g_dump.
 *
 * Algoritmo per ogni settore:
 *  1. Prova tutte le chiavi come Key A: se autentica, salva la chiave e legge tutti i blocchi.
 *  2. Prova tutte le chiavi come Key B: sempre, anche se Key A è già nota, perché
 *     Key B può dare accesso diverso e va salvata nel dump.
 *  3. Dopo aver letto il trailer, ripristina Key A nei byte 0–5: l'hardware
 *     la restituisce sempre come 00×6 per sicurezza, ma la conosciamo dall'auth.
 *  4. Ripristina Key B nei byte 10–15 del trailer.
 *
 * Dopo ogni auth fallita, ri-seleziona il tag (inRelease + readPassive) perché
 * il PN532 porta il tag in uno stato di errore che impedisce ulteriori operazioni.
 *
 * @param sectorsRead Numero di settori letti con successo (output).
 * @return true se almeno un settore è stato letto.
 */
static bool mifareClassicReadDump(uint8_t &sectorsRead) {
    auto keys = loadKeysForUID(g_dump.uid, g_dump.uidLen); // carica chiavi da SD
    sectorsRead = 0;

    for (uint8_t s = 0; s < g_dump.numSectors; s++) {
        uint8_t trailer = trailerBlock(s); // indice del blocco trailer del settore
        uint8_t first = firstBlock(s);     // indice del primo blocco del settore
        uint8_t count = blocksInSector(s); // numero di blocchi nel settore
        bool authenticated = false;

        // ── Tentativo con Key A ────────────────────────────────────────────
        for (auto &key : keys) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 0, key.data()
                )) {
                memcpy(g_dump.keyA[s], key.data(), 6); // salva la chiave che ha funzionato
                g_dump.keyAFound[s] = true;
                authenticated = true;
                break; // non serve provare altre chiavi A
            }
            // Re-selezione dopo auth fallita (il tag va in stato di errore)
            mifareNfc.inRelease(1);
            mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
        }

        // ── Tentativo con Key B (sempre, indipendentemente da Key A) ───────
        for (auto &key : keys) {
            // Prima ri-autentica con Key A per rimettere il tag in stato valido
            if (g_dump.keyAFound[s]) {
                mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 0, g_dump.keyA[s]
                );
            }
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, trailer, 1, key.data()
                )) {
                memcpy(g_dump.keyB[s], key.data(), 6); // salva Key B trovata
                g_dump.keyBFound[s] = true;
                break;
            }
            // Re-selezione dopo auth fallita
            mifareNfc.inRelease(1);
            mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
        }

        // Se nessuna chiave ha funzionato, salta il settore
        if (!authenticated) {
            Serial.printf("[TESSERE] Sector %u: no key found, skipping.\n", s);
            continue;
        }

        // ── Lettura di tutti i blocchi del settore ─────────────────────────
        for (uint8_t b = 0; b < count; b++) {
            uint8_t blockNum = first + b;
            if (mifareNfc.mifareclassic_ReadDataBlock(blockNum, g_dump.data[blockNum]))
                g_dump.blockRead[blockNum] = true; // segna il blocco come letto
            else Serial.printf("[TESSERE] Sector %u block %u read failed.\n", s, blockNum);
        }

        // ── Ripristino chiavi nel trailer (l'hardware le oscura durante la lettura) ──
        if (g_dump.keyAFound[s]) memcpy(g_dump.data[trailer], g_dump.keyA[s], 6);      // Key A: byte 0–5
        if (g_dump.keyBFound[s]) memcpy(g_dump.data[trailer] + 10, g_dump.keyB[s], 6); // Key B: byte 10–15

        sectorsRead++;
    }
    return sectorsRead > 0;
}

/**
 * @brief Legge tutte le pagine leggibili di un tag MIFARE Ultralight in g_dump.
 *
 * MIFARE UL / NTAG213 non richiede autenticazione per leggere le pagine standard.
 * Ogni pagina è di 4 byte; viene salvata allineata a 16 byte in data[][] (padding 0).
 * La lettura si interrompe quando il tag non risponde (fine della memoria).
 *
 * @param pagesRead Numero di pagine lette con successo (output).
 * @return true se almeno una pagina è stata letta.
 */
static bool mifareULReadDump(uint8_t &pagesRead) {
    pagesRead = 0;

    for (uint8_t page = 0; page < MIFARE_UL_MAX_PAGES; page++) {
        uint8_t buf[4];
        if (!mifareNfc.ntag2xx_ReadPage(page, buf)) break; // tag non risponde: fine memoria

        // Salva i 4 byte della pagina nei primi 4 byte del blocco (padding 0 per i restanti 12)
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
 * MIFARE Ultralight (SAK 0x00) non usa autenticazione → mifareULReadDump().
 * Tutti gli altri (MIFARE Classic) usano autenticazione → mifareClassicReadDump().
 *
 * @param sectorsRead Settori (o pagine per UL) letti con successo (output).
 */
bool mifareReadDump(uint8_t &sectorsRead) {
    if (g_dump.sak == 0x00) return mifareULReadDump(sectorsRead); // Ultralight
    return mifareClassicReadDump(sectorsRead);                    // Classic
}

/**
 * @brief Legge tutti i settori del tag MIFARE Classic usando le chiavi in g_dump.
 *
 * Identico a mifareClassicReadDump() ma invece di chiamare loadKeysForUID()
 * costruisce il vettore di chiavi direttamente da g_dump.keyA[] e g_dump.keyB[]
 * dei settori già noti.
 *
 * Sequenza per ogni settore:
 *  1. Se keyAFound[s] è true, tenta l'autenticazione con g_dump.keyA[s].
 *  2. Se keyBFound[s] è true, tenta l'autenticazione con g_dump.keyB[s].
 *  3. Se almeno una autentica, legge tutti i blocchi del settore.
 *  4. Ripristina Key A e Key B nel trailer (l'hardware le oscura durante la lettura).
 */
static bool mifareClassicReadDumpWithKeys(uint8_t &sectorsRead) {
    sectorsRead = 0;

    for (uint8_t s = 0; s < g_dump.numSectors; s++) {
        uint8_t trailer = trailerBlock(s);
        uint8_t first = firstBlock(s);
        uint8_t count = blocksInSector(s);
        bool authenticated = false;

        // ── Tentativo con Key A (se presente nel dump) ─────────────────────
        if (g_dump.keyAFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, first, 0, g_dump.keyA[s]
                )) {
                authenticated = true;
            } else {
                // Re-selezione dopo auth fallita
                mifareNfc.inRelease(1);
                mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
            }
        }

        // ── Tentativo con Key B (se presente nel dump) ─────────────────────
        if (!authenticated && g_dump.keyBFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, first, 1, g_dump.keyB[s]
                )) {
                authenticated = true;
            } else {
                mifareNfc.inRelease(1);
                mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
            }
        }

        // Se nessuna delle due chiavi ha funzionato, salta il settore
        if (!authenticated) {
            Serial.printf("[TESSERE] Sector %u: key auth failed, skipping.\n", s);
            continue;
        }

        // Prova comunque Key B separatamente per salvarla nel dump
        // (anche se Key A ha già autenticato, Key B potrebbe essere diversa)
        if (g_dump.keyAFound[s] && g_dump.keyBFound[s]) {
            // Re-autentica con Key A per rimettere il tag in stato valido
            mifareNfc.mifareclassic_AuthenticateBlock(g_dump.uid, g_dump.uidLen, trailer, 0, g_dump.keyA[s]);
            // Tenta Key B per verificarla
            mifareNfc.mifareclassic_AuthenticateBlock(g_dump.uid, g_dump.uidLen, trailer, 1, g_dump.keyB[s]);
            // Ri-autentica con Key A per la lettura dei blocchi
            mifareNfc.mifareclassic_AuthenticateBlock(g_dump.uid, g_dump.uidLen, trailer, 0, g_dump.keyA[s]);
        }

        // ── Lettura di tutti i blocchi del settore ─────────────────────────
        for (uint8_t b = 0; b < count; b++) {
            uint8_t blockNum = first + b;
            if (mifareNfc.mifareclassic_ReadDataBlock(blockNum, g_dump.data[blockNum])) {
                g_dump.blockRead[blockNum] = true;
            } else {
                Serial.printf("[TESSERE] Sector %u block %u read failed.\n", s, blockNum);
                mifareNfc.inRelease(1);
                mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, g_dump.uid, &g_dump.uidLen, 1000);
                break; // esci dal loop blocchi, passa al settore successivo
            }
        }

        // ── Ripristino chiavi nel trailer ──────────────────────────────────
        // L'hardware restituisce Key A come 00×6 durante la lettura del trailer:
        // la sovrascriviamo con quella che sappiamo essere corretta.
        if (g_dump.keyAFound[s]) memcpy(g_dump.data[trailer], g_dump.keyA[s], 6);
        if (g_dump.keyBFound[s]) memcpy(g_dump.data[trailer] + 10, g_dump.keyB[s], 6);

        sectorsRead++;
    }
    return sectorsRead > 0;
}

/**
 * @brief Dispatcher pubblico: usa le chiavi già in g_dump invece della SD.
 *
 * Supporta solo MIFARE Classic: per Ultralight non serve autenticazione
 * quindi mifareReadDump() va già bene così com'è.
 */
bool mifareReadDumpWithKeys(uint8_t &sectorsRead) {
    if (g_dump.sak == 0x00) {
        // Ultralight non usa chiavi: questa funzione non ha senso qui
        Serial.println("[TESSERE] mifareReadDumpWithKeys: tag Ultralight, usa mifareReadDump().");
        return false;
    }
    return mifareClassicReadDumpWithKeys(sectorsRead);
}

// ─── Scrittura dump ───────────────────────────────────────────────────────────

/**
 * @brief Scrive un dump su tag MIFARE Classic fisico (blocco 0 escluso).
 *
 * Per ogni settore:
 *  1. Autentica con Key A dal dump (fallback Key B).
 *  2. Scrive tutti i blocchi marcati come letti nel dump.
 *  3. Salta esplicitamente il blocco 0 (dati produttore, read-only).
 *  4. Salta i blocchi non presenti nel dump (blockRead[blockNum] == false).
 *
 * Per scrivere anche il blocco 0 su tag magic, usare mifareWriteBlock0().
 *
 * @param src            Dump sorgente da scrivere.
 * @param sectorsWritten Numero di settori scritti con successo (output).
 * @return true se almeno un settore è stato scritto.
 */
static bool mifareClassicWriteDump(const MifareDump &src, uint8_t &sectorsWritten) {
    sectorsWritten = 0;

    for (uint8_t s = 0; s < src.numSectors; s++) {
        uint8_t trailer = trailerBlock(s);
        uint8_t first = firstBlock(s);
        uint8_t count = blocksInSector(s);
        bool authenticated = false;

        // Autentica con Key A dal dump
        if (src.keyAFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.uidLen, trailer, 0, const_cast<uint8_t *>(src.keyA[s])
                ))
                authenticated = true;
        }

        // Fallback: autentica con Key B se Key A ha fallito
        if (!authenticated && src.keyBFound[s]) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.uidLen, trailer, 1, const_cast<uint8_t *>(src.keyB[s])
                ))
                authenticated = true;
        }

        if (!authenticated) {
            Serial.printf("[TESSERE] Write: sector %u auth failed, skipping.\n", s);
            continue; // impossibile scrivere senza autenticazione
        }

        bool sectorOk = true;
        for (uint8_t b = 0; b < count; b++) {
            uint8_t blockNum = first + b;

            // Blocco 0 = dati produttore: sola lettura su tag originali.
            // Per scrivere il blocco 0 su tag magic, usare mifareWriteBlock0().
            if (blockNum == 0) continue;

            // Salta i blocchi non presenti nel dump (non letti durante la read)
            if (!src.blockRead[blockNum]) continue;

            if (!mifareNfc.mifareclassic_WriteDataBlock(
                    blockNum, const_cast<uint8_t *>(src.data[blockNum])
                )) {
                Serial.printf("[TESSERE] Write: block %u failed.\n", blockNum);
                sectorOk = false; // errore su questo blocco, ma continua
            }
        }
        if (sectorOk) sectorsWritten++;
    }
    return sectorsWritten > 0;
}

/**
 * @brief Scrive un dump su tag MIFARE Ultralight fisico.
 *
 * Scrive le pagine presenti nel dump a partire dalla pagina 4.
 * Le pagine 0–3 (OTP, lock bytes, header) vengono saltate per sicurezza
 * perché alcune di esse sono write-once e una scrittura errata è irreversibile.
 *
 * @param src          Dump sorgente.
 * @param pagesWritten Numero di pagine scritte con successo (output).
 * @return true se almeno una pagina è stata scritta.
 */
static bool mifareULWriteDump(const MifareDump &src, uint8_t &pagesWritten) {
    pagesWritten = 0;

    for (uint8_t page = 4; page < MIFARE_UL_MAX_PAGES; page++) {
        if (!src.blockRead[page]) continue; // salta le pagine non presenti nel dump

        uint8_t buf[4];
        memcpy(buf, src.data[page], 4); // copia i 4 byte della pagina

        if (mifareNfc.ntag2xx_WritePage(page, buf)) pagesWritten++;
        else Serial.printf("[TESSERE] UL write page %u failed.\n", page);
    }
    return pagesWritten > 0;
}

/**
 * @brief Dispatcher: chiama la funzione di scrittura corretta in base al SAK del dump.
 */
bool mifareWriteDump(const MifareDump &src, uint8_t &sectorsWritten) {
    if (src.sak == 0x00) return mifareULWriteDump(src, sectorsWritten); // Ultralight
    return mifareClassicWriteDump(src, sectorsWritten);                 // Classic
}

/**
 * @brief Scrive il blocco 0 (dati produttore / UID) del dump su tag magic.
 *
 * Il blocco 0 contiene: UID (4 byte), BCC (byte di controllo XOR dell'UID),
 * SAK, ATQA e dati del produttore. Sui tag originali MIFARE questo blocco
 * è read-only e la scrittura viene ignorata dall'hardware.
 *
 * Questa funzione serve SOLO per i tag magic (CUID, FUID, GEN2, ecc.)
 * che permettono la sovrascrittura del blocco 0, rendendo possibile la
 * clonazione completa di una tessera (UID incluso).
 *
 * Sequenza operativa:
 *  1. Verifica che il blocco 0 sia presente nel dump.
 *  2. Autentica il settore 0 con Key A dal dump (fallback Key B).
 *  3. Scrive il blocco 0 con i 16 byte del dump.
 *
 * @warning Scrivere un UID o BCC errato su tag magic può rendere il tag
 *          non rilevabile dai lettori NFC. Verificare i dati prima di procedere.
 *
 * @param src Dump sorgente contenente il blocco 0 da scrivere.
 * @return true se la scrittura ha avuto successo.
 */
bool mifareWriteBlock0(const MifareDump &src) {
    // Verifica che il blocco 0 sia stato letto e sia presente nel dump
    if (!src.blockRead[0]) {
        Serial.println("[TESSERE] mifareWriteBlock0: blocco 0 non nel dump.");
        return false;
    }

    bool authenticated = false;

    // Autentica il settore 0 con Key A (il trailer del settore 0 è il blocco 3)
    if (src.keyAFound[0]) {
        authenticated = mifareNfc.mifareclassic_AuthenticateBlock(
            const_cast<uint8_t *>(src.uid), src.uidLen, 3, 0, const_cast<uint8_t *>(src.keyA[0])
        );
    }

    // Fallback: autentica con Key B se Key A ha fallito
    if (!authenticated && src.keyBFound[0]) {
        authenticated = mifareNfc.mifareclassic_AuthenticateBlock(
            const_cast<uint8_t *>(src.uid), src.uidLen, 3, 1, const_cast<uint8_t *>(src.keyB[0])
        );
    }

    if (!authenticated) {
        Serial.println("[TESSERE] mifareWriteBlock0: autenticazione settore 0 fallita.");
        return false;
    }

    // Scrive il blocco 0 con i dati del dump
    bool ok = mifareNfc.mifareclassic_WriteDataBlock(0, const_cast<uint8_t *>(src.data[0]));

    if (ok) Serial.println("[TESSERE] mifareWriteBlock0: scritto con successo.");
    else Serial.println("[TESSERE] mifareWriteBlock0: fallito (tag non magic?).");

    return ok;
}

// ─── Persistenza su SD ───────────────────────────────────────────────────────

/**
 * @brief Serializza il dump in un file binario su SD.
 *
 * Crea la cartella /rfid/dumps/ se non esiste.
 * Sovrascrive eventuali dump precedenti per lo stesso UID.
 *
 * Formato binario (little-endian, ~4.9 KB totali):
 *   4 byte   → magic "MFDR" (identifica il formato)
 *   1 byte   → version (versione formato, attualmente 1)
 *   1 byte   → uidLen
 *   7 byte   → uid (padding 0 per UID < 7 byte)
 *   1 byte   → sak
 *   2 byte   → atqa
 *   1 byte   → numSectors
 *   240 byte → keyA[40][6]
 *   40 byte  → keyAFound[40]
 *   240 byte → keyB[40][6]
 *   40 byte  → keyBFound[40]
 *   256 byte → blockRead[256]
 *   4096 byte → data[256][16]
 *
 * @param dump   Dump da salvare.
 * @param uidHex Stringa esadecimale dell'UID (usata come nome file).
 * @return true se la scrittura è riuscita.
 */
bool saveDumpToSD(const MifareDump &dump, const String &uidHex) {
    // Crea le cartelle se non esistono
    if (!SD.exists("/rfid")) SD.mkdir("/rfid");
    if (!SD.exists("/rfid/dumps")) SD.mkdir("/rfid/dumps");

    String path = "/rfid/dumps/" + uidHex + ".bin";
    File f = SD.open(path, FILE_WRITE); // FILE_WRITE sovrascrive se esiste
    if (!f) {
        Serial.printf("[TESSERE] saveDump: cannot open %s\n", path.c_str());
        return false;
    }

    // Scrive l'header del file
    f.write((const uint8_t *)DUMP_MAGIC, 4); // magic "MFDR"
    uint8_t ver = DUMP_VERSION;
    f.write(&ver, 1);
    f.write(&dump.uidLen, 1);
    f.write(dump.uid, 7);
    f.write(&dump.sak, 1);
    f.write((const uint8_t *)&dump.atqa, 2);
    f.write(&dump.numSectors, 1);

    // Scrive le chiavi per settore
    f.write((const uint8_t *)dump.keyA, sizeof(dump.keyA));
    f.write((const uint8_t *)dump.keyAFound, sizeof(dump.keyAFound));
    f.write((const uint8_t *)dump.keyB, sizeof(dump.keyB));
    f.write((const uint8_t *)dump.keyBFound, sizeof(dump.keyBFound));

    // Scrive i flag di lettura e i dati EEPROM
    f.write((const uint8_t *)dump.blockRead, sizeof(dump.blockRead));
    f.write((const uint8_t *)dump.data, sizeof(dump.data));

    f.close();
    Serial.printf("[TESSERE] Dump saved to %s\n", path.c_str());
    return true;
}

/**
 * @brief Deserializza un dump da un file binario su SD nella struct @p dump.
 *
 * Verifica la firma magic e la versione prima di procedere.
 * Se la versione non corrisponde, rifiuta il file per evitare corruzione.
 * Ripristina tagType e numSectors dal SAK (non salvati direttamente).
 *
 * @param dump Struttura di destinazione.
 * @param path Percorso assoluto del file (es. "/rfid/dumps/AABBCCDD.bin").
 * @return true se il file è valido e caricato correttamente.
 */
bool loadDumpFromSD(MifareDump &dump, const String &path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[TESSERE] loadDump: cannot open %s\n", path.c_str());
        return false;
    }

    // Verifica la firma magic (primi 4 byte del file)
    char magic[4];
    f.read((uint8_t *)magic, 4);
    if (memcmp(magic, DUMP_MAGIC, 4) != 0) {
        Serial.println("[TESSERE] loadDump: firma magic non valida.");
        f.close();
        return false;
    }

    // Verifica la versione del formato
    uint8_t ver;
    f.read(&ver, 1);
    if (ver != DUMP_VERSION) {
        Serial.printf("[TESSERE] loadDump: versione %u non supportata.\n", ver);
        f.close();
        return false;
    }

    // Legge i campi dell'header
    f.read(&dump.uidLen, 1);
    f.read(dump.uid, 7);
    f.read(&dump.sak, 1);
    f.read((uint8_t *)&dump.atqa, 2);
    f.read(&dump.numSectors, 1);

    // Ripristina i campi derivati non serializzati
    dump.tagType = getTagType(dump.sak); // ricostruisce la stringa dal SAK

    // Legge le chiavi per settore
    f.read((uint8_t *)dump.keyA, sizeof(dump.keyA));
    f.read((uint8_t *)dump.keyAFound, sizeof(dump.keyAFound));
    f.read((uint8_t *)dump.keyB, sizeof(dump.keyB));
    f.read((uint8_t *)dump.keyBFound, sizeof(dump.keyBFound));

    // Legge i flag di lettura e i dati EEPROM
    f.read((uint8_t *)dump.blockRead, sizeof(dump.blockRead));
    f.read((uint8_t *)dump.data, sizeof(dump.data));

    f.close();
    Serial.printf(
        "[TESSERE] Dump loaded: %s (UID=%s)\n", path.c_str(), buildUIDHex(dump.uid, dump.uidLen).c_str()
    );
    return true;
}
