/**
 * @file TessereMenuLogic.cpp
 * @brief Implementazione delle azioni di menu Tessere (UI + bridge verso TessereLogic).
 *
 * Questo file contiene tutta la logica di presentazione: titoli, feedback
 * di avanzamento, selezione file da SD e gestione degli errori.
 * Nessuna logica NFC risiede qui: ogni operazione sul tag è delegata
 * all'API dichiarata in TessereLogic.h.
 *
 * Azioni implementate:
 *   - InfoTessera()        → mostra UID, SAK, ATQA, tipo tag
 *   - ReadTessera()        → legge dump e salva su SD
 *   - WriteTessera()       → scrive dump selezionato dalla SD
 *   - AutoWriteTessera()   → scrive automaticamente il dump per l'UID rilevato
 */

#include "TessereMenuLogic.h"
#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <SD.h>

// ─── Helper UI (statico interno) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza il corpo sulle righe (separatore '\n') e rispetta i margini
 * dello schermo di Bruce tramite padprintln. Timeout minimo di 300 ms.
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

// ─── Azioni menu ─────────────────────────────────────────────────────────────

/**
 * @brief Identifica il tag sul lettore e mostra UID, SAK, ATQA e tipo.
 *
 * Non legge i blocchi dati: si limita al rilevamento (waitForMifareTag),
 * quindi è veloce e non richiede chiavi di autenticazione.
 */
void InfoTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    // Costruisce la stringa UID esadecimale per la visualizzazione
    String uidStr = buildUIDHex(g_dump.uid, g_dump.uidLen);

    char sakStr[5], atqaStr[5];
    snprintf(sakStr, sizeof(sakStr), "%02X", g_dump.sak);
    snprintf(atqaStr, sizeof(atqaStr), "%04X", g_dump.atqa);

    drawMainBorderWithTitle("Info");
    setPadCursor(1, 2);
    padprintln("UID:  " + uidStr);
    padprintln("SAK:  0x" + String(sakStr));
    padprintln("ATQA: 0x" + String(atqaStr));
    padprintln("Type: " + g_dump.tagType);

    delay(300);
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

/**
 * @brief Legge il dump completo del tag e lo salva su SD.
 *
 * Flusso:
 *   1. Inizializza il PN532.
 *   2. Rileva il tag e ne legge l'identità.
 *   3. Mostra avanzamento ("Reading sectors...").
 *   4. Chiama mifareReadDump() che prova le chiavi da /rfid/chiavi.txt.
 *   5. Salva il dump in /rfid/dumps/<UIDHEX>.bin.
 *   6. Mostra il riepilogo (settori letti / totale, percorso file).
 */
void ReadTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);

    // Mostra avanzamento durante la lettura (può richiedere alcuni secondi)
    drawMainBorderWithTitle("Read");
    setPadCursor(1, 2);
    padprintln("UID: " + uidHex);
    padprintln("Type: " + g_dump.tagType);
    padprintln("Reading sectors...");
    padprintln("Keep tag on reader!");

    uint8_t sectorsRead = 0;
    if (!mifareReadDump(sectorsRead)) {
        showMessage("Read", "No sectors readable.\nCheck /rfid/chiavi.txt");
        return;
    }

    // Salva il dump su SD
    if (!saveDumpToSD(g_dump, uidHex)) {
        showMessage("Read", "Read ok but SD save\nfailed!");
        return;
    }

    String resultPath = "/rfid/dumps/" + uidHex + ".bin";
    String msg = "Done!\n";
    msg += "Sectors: " + String(sectorsRead) + "/" + String(g_dump.numSectors) + "\n";
    msg += "Saved:\n" + resultPath;
    showMessage("Read", msg);
}

/**
 * @brief Scrive sul tag fisico un dump scelto dalla lista di file su SD.
 *
 * Flusso:
 *   1. Inizializza il PN532.
 *   2. Elenca i file .bin in /rfid/dumps/.
 *   3. L'utente seleziona il file desiderato.
 *   4. Mostra l'UID contenuto nel dump e chiede conferma.
 *   5. In caso di conferma, attende il tag e scrive il dump.
 *   6. Mostra il riepilogo dei settori scritti.
 *
 * @note La scrittura viene effettuata dopo la conferma, non prima,
 *       per evitare scritture accidentali.
 */
void WriteTessera() {
    if (!mifareInit()) return;

    if (!SD.exists("/rfid/dumps")) {
        showMessage("Write", "No /rfid/dumps folder\non SD.");
        return;
    }

    // Elenca i file .bin disponibili
    File dir = SD.open("/rfid/dumps");
    std::vector<Option> fileOpts;

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        String name = String(entry.name());
        entry.close();
        if (!name.endsWith(".bin")) continue;

        fileOpts.push_back(
            {name.c_str(),
             [name]() {
                 String path = "/rfid/dumps/" + name;

                 // Carica il dump selezionato
                 MifareDump loadedDump;
                 if (!loadDumpFromSD(loadedDump, path)) {
                     showMessage("Write", "Cannot load dump:\n" + path);
                     return;
                 }

                 // Mostra info dump e chiede conferma prima di scrivere
                 String dumpUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                 String confirmMsg = "Write dump?\nUID: " + dumpUID + "\nType: " + loadedDump.tagType;

                 std::vector<Option> confirm = {
                     {"Yes, write",
                      [loadedDump]() mutable {
                          // Attende il tag target sul lettore
                          drawMainBorderWithTitle("Write");
                          setPadCursor(1, 2);
                          padprintln("Place target tag\non reader...");

                          uint8_t uid[7], uidLen;
                          if (!waitForAnyMifareTag(uid, &uidLen)) {
                              showMessage("Write", "No tag found.");
                              return;
                          }

                          // Avvisa se l'UID del tag differisce dal dump
                          // (es. scrittura su tag blank o clone)
                          String targetUID = buildUIDHex(uid, uidLen);
                          String dumpUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                          if (targetUID != dumpUID) {
                              showMessage(
                                  "Write",
                                  "Warning: UID mismatch!\nDump: " + dumpUID + "\nTag:  " + targetUID +
                                      "\nContinuing..."
                              );
                          }

                          // Scrittura effettiva
                          drawMainBorderWithTitle("Write");
                          setPadCursor(1, 2);
                          padprintln("Writing...");
                          padprintln("Keep tag on reader!");

                          uint8_t sectorsWritten = 0;
                          if (!mifareWriteDump(loadedDump, sectorsWritten)) {
                              showMessage("Write", "Write failed!\nNo sectors written.");
                              return;
                          }

                          String msg = "Done!\nSectors: " + String(sectorsWritten) + "/" +
                                       String(loadedDump.numSectors);
                          showMessage("Write", msg);
                      },                     false},
                     {"Cancel",     []() {}, false},
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU, confirmMsg.c_str());
             },
             false}
        );
    }
    dir.close();

    if (fileOpts.empty()) {
        showMessage("Write", "No .bin files in\n/rfid/dumps/");
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Select dump");
}

/**
 * @brief Legge l'UID del tag e scrive automaticamente il dump corrispondente.
 *
 * Cerca il file /rfid/dumps/<UIDHEX>.bin sulla SD: se trovato, lo carica
 * e lo scrive senza interazione aggiuntiva. Se non trovato, mostra l'UID
 * rilevato e suggerisce di usare "Read" per creare il dump prima.
 *
 * Caso d'uso tipico: badge/tessere di cui si possiede già il dump,
 * da ripristinare rapidamente avvicinando il tag al lettore.
 */
void AutoWriteTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);
    String path = "/rfid/dumps/" + uidHex + ".bin";

    Serial.printf("[TESSERE] AutoWrite: looking for %s\n", path.c_str());

    if (!SD.exists(path)) {
        showMessage(
            "Auto Write", "No dump found for:\n" + uidHex + "\nUse 'Read' first to\ncreate the dump."
        );
        return;
    }

    // Carica il dump corrispondente all'UID rilevato
    MifareDump loadedDump;
    if (!loadDumpFromSD(loadedDump, path)) {
        showMessage("Auto Write", "Cannot load dump.");
        return;
    }

    // Scrittura automatica: mostra avanzamento, nessuna ulteriore conferma
    drawMainBorderWithTitle("Auto Write");
    setPadCursor(1, 2);
    padprintln("UID: " + uidHex);
    padprintln("Dump found!");
    padprintln("Writing...");
    padprintln("Keep tag on reader!");

    uint8_t sectorsWritten = 0;
    if (!mifareWriteDump(loadedDump, sectorsWritten)) {
        showMessage("Auto Write", "Write failed!\nNo sectors written.");
        return;
    }

    String msg = "Done!\nUID: " + uidHex + "\nSectors: " + String(sectorsWritten) + "/" +
                 String(loadedDump.numSectors);
    showMessage("Auto Write", msg);
}
