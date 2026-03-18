/**
 * @file TessereMenuLogic.cpp
 * @brief Implementazione delle azioni di menu Tessere (UI + bridge verso TessereLogic).
 *
 * Questo file contiene tutta la logica di presentazione: titoli, feedback
 * di avanzamento, selezione file da SD e gestione degli errori.
 * Nessuna logica NFC risiede qui: ogni operazione sul tag è delegata
 * all'API dichiarata in TessereLogic.h.
 *
 * Helper UI interni:
 *   - showMessage() → mostra titolo + corpo e attende un tasto
 *   - showInfo()    → mostra titolo + corpo senza attendere (messaggi di stato)
 *
 * Azioni implementate:
 *   - InfoTessera()      → mostra UID, SAK, ATQA, tipo e gestore del tag
 *   - ReadTessera()      → legge dump, salva su SD, chiede associazione gestore
 *   - WriteTessera()     → scrive dump selezionato dalla SD
 *   - AutoWriteTessera() → scrive automaticamente il dump per l'UID rilevato
 *   - GestoriMenu()      → gestisce la lista gestori (aggiungi/modifica/elimina)
 */

#include "TessereMenuLogic.h"
#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <SD.h>

// ─── Helper UI (statici interni) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza il corpo sulle righe (separatore '\n') e rispetta i margini
 * dello schermo di Bruce tramite padprintln. Timeout minimo di 300 ms.
 * Da usare per risultati finali, errori e conferme.
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

/**
 * @brief Mostra un pannello con titolo e corpo testuale senza attendere input.
 *
 * A differenza di showMessage(), non aspetta la pressione di un tasto.
 * Da usare per messaggi di stato intermedi durante operazioni NFC
 * (es. "Place tag on reader...", "Reading sectors...", "Writing...").
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
}

// ─── Azioni menu ─────────────────────────────────────────────────────────────

/**
 * @brief Identifica il tag sul lettore e mostra UID, SAK, ATQA, tipo e gestore.
 *
 * Non legge i blocchi dati: si limita al rilevamento (waitForMifareTag),
 * quindi è veloce e non richiede chiavi di autenticazione.
 * Mostra anche se esiste un dump salvato per questo tag.
 */
void InfoTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidStr = buildUIDHex(g_dump.uid, g_dump.uidLen);

    char sakStr[5], atqaStr[5];
    snprintf(sakStr, sizeof(sakStr), "%02X", g_dump.sak);
    snprintf(atqaStr, sizeof(atqaStr), "%04X", g_dump.atqa);

    // Controlla se esiste un dump salvato per questo tag
    String dumpPath = "/rfid/dumps/" + uidStr + ".bin";
    String dumpStatus = SD.exists(dumpPath) ? "Saved: YES" : "Saved: NO";

    // Cerca il gestore associato all'UID
    String gestore = lookupGestore(uidStr);
    String gestoreStr = gestore.isEmpty() ? "unknown" : gestore;

    showMessage(
        "Info",
        "UID:  " + uidStr + "\n" + "SAK:  0x" + String(sakStr) + "\n" + "ATQA: 0x" + String(atqaStr) + "\n" +
            "Type: " + g_dump.tagType + "\n" + "Gestore: " + gestoreStr + "\n" + dumpStatus
    );
}

/**
 * @brief Legge il dump completo del tag, lo salva su SD e chiede di associare un gestore.
 *
 * Flusso:
 *   1. Inizializza il PN532.
 *   2. Rileva il tag e ne legge l'identità.
 *   3. Mostra avanzamento con showInfo() (nessuna attesa tasto).
 *   4. Chiama mifareReadDump() che prova le chiavi da /rfid/chiavi.txt.
 *   5. Salva il dump in /rfid/dumps/<UIDHEX>.bin.
 *   6. Mostra il riepilogo con showMessage().
 *   7. Chiede yes/no se associare un gestore dalla lista in /rfid/gestori.txt.
 *      Se il tag aveva già un gestore associato, lo mostra come suggerimento.
 */
void ReadTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);

    // Stato intermedio: mostra avanzamento senza aspettare input
    showInfo(
        "Read",
        "UID: " + uidHex + "\n" + "Type: " + g_dump.tagType + "\n" +
            "Reading sectors...\n"
            "Keep tag on reader!"
    );

    uint8_t sectorsRead = 0;
    if (!mifareReadDump(sectorsRead)) {
        showMessage("Read", "No sectors readable.\nCheck /rfid/chiavi.txt");
        return;
    }

    if (!saveDumpToSD(g_dump, uidHex)) {
        showMessage("Read", "Read ok but SD save\nfailed!");
        return;
    }

    String resultPath = "/rfid/dumps/" + uidHex + ".bin";
    showMessage(
        "Read",
        "Done!\n"
        "Sectors: " +
            String(sectorsRead) + "/" + String(g_dump.numSectors) + "\n" + "Saved:\n" + resultPath
    );

    // Chiede se associare un gestore alla tessera appena letta
    // Il titolo del menu mostra il gestore attuale se già presente
    String gestoreAttuale = lookupGestore(uidHex);
    String prompt =
        gestoreAttuale.isEmpty() ? "Associate gestore?" : "Gestore: " + gestoreAttuale + "\nChange?";

    std::vector<Option> gestoreOpts = {
        {"Yes",
         [uidHex]() {
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage(
                     "Gestori",
                     "No gestori found.\n"
                     "Add one from\n"
                     "Gestori menu first."
                 );
                 return;
             }

             // Mostra la lista dei gestori disponibili
             std::vector<Option> gOpts;
             for (auto &g : list) {
                 gOpts.push_back(
                     {g.c_str(),
                      [uidHex, g]() {
                          if (associateGestore(uidHex, g))
                              showMessage("Read", "Associated:\n" + uidHex + "\nVending operators: " + g);
                          else showMessage("Read", "Error saving\nassociation.");
                      },
                      false}
                 );
             }
             loopOptions(gOpts, MENU_TYPE_SUBMENU, "Select gestore");
         },              false},
        {"No",  []() {}, false},
    };
    loopOptions(gestoreOpts, MENU_TYPE_SUBMENU, prompt.c_str());
}

/**
 * @brief Scrive sul tag fisico un dump scelto dalla lista di file su SD.
 *
 * Flusso:
 *   1. Inizializza il PN532.
 *   2. Elenca i file .bin in /rfid/dumps/.
 *   3. L'utente seleziona il file desiderato.
 *   4. Mostra l'UID contenuto nel dump e chiede conferma.
 *   5. In caso di conferma, attende il tag con showInfo() e scrive il dump.
 *   6. Mostra il riepilogo con showMessage().
 *
 * @note La scrittura viene effettuata dopo la conferma per evitare
 *       scritture accidentali. MifareDump è dichiarata static per
 *       evitare overflow dello stack dell'ESP32 (~5KB).
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

                 // static: evita la copia da ~5KB sullo stack dell'ESP32
                 static MifareDump loadedDump;
                 if (!loadDumpFromSD(loadedDump, path)) {
                     showMessage("Write", "Cannot load dump:\n" + path);
                     return;
                 }

                 String dumpUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);

                 std::vector<Option> confirm = {
                     {"Yes, write",
                      []() {
                          // Stato intermedio: attende tag senza bloccare su tasto
                          showInfo("Write", "Place target tag\non reader...");

                          uint8_t uid[7], uidLen;
                          if (!waitForAnyMifareTag(uid, &uidLen)) {
                              showMessage("Write", "No tag found.");
                              return;
                          }

                          // Avvisa se l'UID del tag differisce dal dump
                          // (utile per scrittura su tag blank o clone)
                          String targetUID = buildUIDHex(uid, uidLen);
                          String srcUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                          if (targetUID != srcUID) {
                              showMessage(
                                  "Write",
                                  "Warning: UID mismatch!\n"
                                  "Dump: " +
                                      srcUID + "\n" + "Tag:  " + targetUID +
                                      "\n"
                                      "Continuing..."
                              );
                          }

                          // Stato intermedio: scrittura in corso
                          showInfo("Write", "Writing...\nKeep tag on reader!");

                          uint8_t sectorsWritten = 0;
                          if (!mifareWriteDump(loadedDump, sectorsWritten)) {
                              showMessage("Write", "Write failed!\nNo sectors written.");
                              return;
                          }

                          showMessage(
                              "Write",
                              "Done!\nSectors: " + String(sectorsWritten) + "/" +
                                  String(loadedDump.numSectors)
                          );
                      },                     false},
                     {"Cancel",     []() {}, false},
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU);
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
 *
 * @note MifareDump è dichiarata static per evitare overflow dello stack (~5KB).
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

    // static: evita la copia da ~5KB sullo stack dell'ESP32
    static MifareDump loadedDump;
    if (!loadDumpFromSD(loadedDump, path)) {
        showMessage("Auto Write", "Cannot load dump.");
        return;
    }

    // Stato intermedio: scrittura in corso, nessuna attesa tasto
    showInfo(
        "Auto Write",
        "UID: " + uidHex +
            "\n"
            "Dump found!\n"
            "Writing...\n"
            "Keep tag on reader!"
    );

    uint8_t sectorsWritten = 0;
    if (!mifareWriteDump(loadedDump, sectorsWritten)) {
        showMessage("Auto Write", "Write failed!\nNo sectors written.");
        return;
    }

    showMessage(
        "Auto Write",
        "Done!\n"
        "UID: " +
            uidHex + "\n" + "Sectors: " + String(sectorsWritten) + "/" + String(loadedDump.numSectors)
    );
}

/**
 * @brief Menu di gestione dei gestori: Aggiungi, Modifica, Elimina.
 *
 * Non richiede NFC: opera solo su file SD (/rfid/gestori.txt e
 * /rfid/gestori_map.txt). Le modifiche ai nomi vengono propagate
 * automaticamente a tutte le associazioni UID esistenti.
 */
void GestoriMenu() {
    std::vector<Option> opts = {

        // ── Aggiungi ──────────────────────────────────────────────────────
        {"Add",
         []() {
             String nome = keyboard("", 20, "Nome gestore:");
             if (nome.isEmpty()) {
                 showMessage("Gestori", "Cancelled.");
                 return;
             }
             if (addGestore(nome)) showMessage("Gestori", "Added:\n" + nome);
             else showMessage("Gestori", "Already exists:\n" + nome);
         }, false},

        // ── Modifica ──────────────────────────────────────────────────────
        {"Edit",
         []() {
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage("Gestori", "No gestori found.");
                 return;
             }

             // Mostra la lista e lascia selezionare quello da rinominare
             std::vector<Option> selectOpts;
             for (auto &g : list) {
                 selectOpts.push_back(
                     {g.c_str(),
                      [g]() {
                          // Pre-compila il campo con il nome attuale per comodità
                          String newNome = keyboard(g, 20, "New name:");
                          if (newNome.isEmpty() || newNome == g) {
                              showMessage("Gestori", "Cancelled.");
                              return;
                          }
                          if (modifyGestore(g, newNome))
                              showMessage("Gestori", "Renamed:\nOld:" + g + "\nNew:" + newNome);
                          else showMessage("Gestori", "Error renaming.");
                      },
                      false}
                 );
             }
             loopOptions(selectOpts, MENU_TYPE_SUBMENU, "Select gestore");
         }, false},

        // ── Elimina ───────────────────────────────────────────────────────
        {"Delete",
         []() {
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage("Gestori", "No gestori found.");
                 return;
             }

             // Mostra la lista e lascia selezionare quello da eliminare
             std::vector<Option> selectOpts;
             for (auto &g : list) {
                 selectOpts.push_back(
                     {g.c_str(),
                      [g]() {
                          // Chiede conferma prima di eliminare
                          std::vector<Option> confirm = {
                              {"Yes, delete",
         [g]() {
                                   if (deleteGestore(g)) showMessage("Gestori", "Deleted:\n" + g);
                                   else showMessage("Gestori", "Error deleting.");
                               }, false},
                              {"Cancel", []() {}, false},
                          };
                          loopOptions(confirm, MENU_TYPE_SUBMENU, ("Delete " + g + "?").c_str());
                      },
                      false}
                 );
             }
             loopOptions(selectOpts, MENU_TYPE_SUBMENU, "Select gestore");
         },           false                },
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Gestori");
}
