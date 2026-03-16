/**
 * @file MikaiMenuLogic.cpp
 * @brief Implementazione delle azioni di menu Mikai (UI + bridge verso MikaiLogic).
 *
 * Questo file contiene tutta la logica di presentazione: disegno dei titoli,
 * attesa dell'input utente, selezione di importi/file e gestione degli errori.
 * Non contiene logica di dominio: ogni operazione sul tag è delegata all'API
 * mikai_* dichiarata in MikaiLogic.h.
 *
 * Helper interni (statici):
 *   - showMessage() → mostra un titolo + corpo testuale e attende un tasto.
 *   - loadTag()     → istruisce l'utente a posizionare il tag e lo legge.
 */

#include "MikaiMenuLogic.h"
#include "MikaiLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <SD.h>

// ─── Helper UI ───────────────────────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza automaticamente il corpo sulle righe (separatore '\n') e
 * usa padprintln per rispettare i margini dello schermo di Bruce.
 * Il timeout minimo di 300 ms evita attivazioni accidentali.
 *
 * @param title Testo del bordo/titolo superiore.
 * @param body  Testo da mostrare, con '\n' come separatore di riga.
 */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    // Stampa riga per riga
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
 * @brief Istruisce l'utente a posizionare il tag sul lettore e lo legge.
 *
 * Mostra il messaggio "Place MyKey on reader..." nell'area principale
 * e chiama mikai_read_tag(). In caso di fallimento mostra un errore
 * tramite showMessage() e restituisce false.
 *
 * @return true se il tag è stato letto correttamente.
 */
static bool loadTag() {
    drawMainBorderWithTitle("Mikai");
    setPadCursor(1, 2);
    padprintln("Place MyKey on reader...");

    if (!mikai_read_tag(&srixKey, &nfc)) {
        showMessage("Mikai", "Tag read failed.");
        return false;
    }
    return true;
}

// ─── Azioni menu ─────────────────────────────────────────────────────────────

/**
 * @brief Legge il tag e mostra le informazioni complete a schermo.
 *
 * Le informazioni (Lock-ID, credito, SK, data di produzione, storico
 * transazioni) vengono generate da mikai_get_info_string() e visualizzate
 * riga per riga tramite strtok.
 */
void actionInfo() {
    if (!loadTag()) return;

    char buf[800];
    mikai_get_info_string(&srixKey, buf, sizeof(buf));

    drawMainBorderWithTitle("Info");
    setPadCursor(1, 2);

    char *line = strtok(buf, "\n");
    while (line != nullptr) {
        padprintln(line);
        line = strtok(nullptr, "\n");
    }

    delay(300);
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

/**
 * @brief Scrive sul tag fisico tutti i blocchi modificati in memoria.
 *
 * Se non ci sono blocchi in attesa, avvisa l'utente. Altrimenti istruisce
 * l'utente a mantenere il tag sul lettore e chiama mikai_write_modified_blocks().
 * Al termine mostra un messaggio di conferma.
 */
void actionWrite() {
    if (!mikai_has_pending_writes(&srixKey)) {
        showMessage("Write", "No pending changes.");
        return;
    }

    drawMainBorderWithTitle("Write");
    setPadCursor(1, 2);
    padprintln("Writing blocks...");
    padprintln("Keep tag on reader!");

    mikai_write_modified_blocks(&srixKey, &nfc);
    showMessage("Write", "Done!");
}

/**
 * @brief Permette di aggiungere un importo preselezionato al credito del tag.
 *
 * Mostra un sottomenu con 10 tagli (da 0.05 a 50.00 EUR). Alla selezione:
 *   1. Chiama mikai_add_cents() con una data fissa (01/01/26).
 *   2. Se riuscita, mostra il nuovo credito e propone di scrivere sul tag.
 *   3. In caso di errore, mostra il codice di errore.
 *
 * @note La data è hardcoded come placeholder; in futuro potrebbe essere
 *       letta dall'RTC del dispositivo.
 */
void actionAddCredit() {
    if (!loadTag()) return;

    // Tabella degli importi disponibili (etichetta + centesimi)
    struct {
        const char *label;
        uint16_t cents;
    } amounts[] = {
        {"0.05 EUR",  5   },
        {"0.10 EUR",  10  },
        {"0.20 EUR",  20  },
        {"0.50 EUR",  50  },
        {"1.00 EUR",  100 },
        {"2.00 EUR",  200 },
        {"5.00 EUR",  500 },
        {"10.00 EUR", 1000},
        {"20.00 EUR", 2000},
        {"50.00 EUR", 5000},
    };
    const int N = sizeof(amounts) / sizeof(amounts[0]);

    std::vector<Option> opts;
    for (int i = 0; i < N; i++) {
        uint16_t c = amounts[i].cents;
        opts.push_back(
            {amounts[i].label,
             [c]() {
                 uint8_t d = 1, m = 1, y = 26; // data placeholder (01/01/2026)

                 int res = mikai_add_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[40];
                     snprintf(buf, sizeof(buf), "New credit: %u.%02u EUR", newc / 100, newc % 100);

                     // Sottomenu di conferma: scrivi o annulla
                     std::vector<Option> confirm = {
                         {"Write to tag", []() { actionWrite(); }, false},
                         {"Cancel",       []() {},                 false},
                     };
                     loopOptions(confirm, MENU_TYPE_SUBMENU, buf);
                 } else {
                     char buf[40];
                     snprintf(buf, sizeof(buf), "Error %d", res);
                     showMessage("Add credit", String(buf));
                 }
             },
             false}
        );
    }
    loopOptions(opts, MENU_TYPE_SUBMENU, "Add credit");
}

/**
 * @brief Permette di impostare il credito a un valore esatto, azzerando lo storico.
 *
 * Mostra un sottomenu con 8 tagli (da 0.50 a 50.00 EUR). Alla selezione:
 *   1. Chiama mikai_set_cents() con una data fissa (01/01/26).
 *   2. Se riuscita, mostra il credito impostato e propone di scrivere.
 *   3. In caso di errore, mostra il codice di errore.
 */
void actionSetCredit() {
    if (!loadTag()) return;

    // Tabella degli importi disponibili
    struct {
        const char *label;
        uint16_t cents;
    } amounts[] = {
        {"0.50 EUR",  50  },
        {"1.00 EUR",  100 },
        {"2.00 EUR",  200 },
        {"5.00 EUR",  500 },
        {"10.00 EUR", 1000},
        {"15.00 EUR", 1500},
        {"20.00 EUR", 2000},
        {"50.00 EUR", 5000},
    };
    const int N = sizeof(amounts) / sizeof(amounts[0]);

    std::vector<Option> opts;
    for (int i = 0; i < N; i++) {
        uint16_t c = amounts[i].cents;
        opts.push_back(
            {amounts[i].label,
             [c]() {
                 uint8_t d = 1, m = 1, y = 26; // data placeholder (01/01/2026)

                 int res = mikai_set_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[40];
                     snprintf(buf, sizeof(buf), "Credit set: %u.%02u EUR", newc / 100, newc % 100);

                     // Sottomenu di conferma: scrivi o annulla
                     std::vector<Option> confirm = {
                         {"Write to tag", []() { actionWrite(); }, false},
                         {"Cancel",       []() {},                 false},
                     };
                     loopOptions(confirm, MENU_TYPE_SUBMENU, buf);
                 } else {
                     char buf[40];
                     snprintf(buf, sizeof(buf), "Error %d", res);
                     showMessage("Set credit", String(buf));
                 }
             },
             false}
        );
    }
    loopOptions(opts, MENU_TYPE_SUBMENU, "Set credit");
}

/**
 * @brief Esporta i blocchi vendor (0x18–0x19) del tag in un file .bin sulla SD.
 *
 * Flusso:
 *   1. Legge il tag; se è in reset (nessun vendor) avvisa e ritorna.
 *   2. Chiede il nome del file tramite la keyboard di Bruce.
 *   3. Crea la cartella /vendor sulla SD se non esiste.
 *   4. Scrive un file /vendor/mikai_<nome>.bin di 8 byte.
 */
void actionExportVendor() {
    if (!loadTag()) return;

    uint8_t buf[8];
    if (mikai_export_vendor(&srixKey, buf) < 0) {
        showMessage("Export vendor", "Key is not bound\nto any vendor.");
        return;
    }

    String filename = keyboard("vendor", 20, "File name:");
    if (filename == "") {
        showMessage("Export vendor", "Cancelled.");
        return;
    }

    // Crea la directory /vendor se non esiste
    if (!SD.exists("/vendor")) SD.mkdir("/vendor");

    String path = "/vendor/mikai_" + filename + ".bin";
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        showMessage("Export vendor", "SD write failed.");
        return;
    }
    f.write(buf, 8);
    f.close();

    showMessage("Export vendor", "Saved to:\n" + path);
}

/**
 * @brief Riporta il tag allo stato di reset di fabbrica e propone la scrittura.
 *
 * Legge il tag, chiama mikai_reset_key() (che marca tutti i blocchi
 * necessari come modificati) e poi presenta il sottomenu di conferma.
 */
void actionReset() {
    if (!loadTag()) return;

    mikai_reset_key(&srixKey);

    std::vector<Option> confirm = {
        {"Write to tag", []() { actionWrite(); }, false},
        {"Cancel",       []() {},                 false},
    };
    loopOptions(confirm, MENU_TYPE_SUBMENU, "Write reset?");
}

/**
 * @brief Importa blocchi vendor da un file .bin sulla SD nel tag.
 *
 * Flusso:
 *   1. Legge il tag; se non è in stato di reset, chiede di fare Reset prima.
 *   2. Elenca i file .bin presenti in /vendor sulla SD.
 *   3. All'utente che seleziona un file, chiede conferma prima di procedere.
 *   4. Ri-legge il tag (per sicurezza) e chiama mikai_import_vendor().
 *   5. Scrive immediatamente i blocchi modificati sul tag fisico.
 *
 * @note La ri-lettura del tag nel sottomenu di conferma garantisce che il
 *       tag non sia stato rimosso e sostituito tra la selezione del file
 *       e la scrittura effettiva.
 */
void actionImportVendor() {
    if (!loadTag()) return;

    if (!mikai_is_reset(&srixKey)) {
        showMessage("Import vendor", "Key is already bound!\nDo a Reset first, then import vendor.");
        return;
    }

    if (!SD.exists("/vendor")) {
        showMessage("Import vendor", "No /vendor folder on SD.");
        return;
    }

    // Elenca tutti i file .bin nella cartella /vendor
    File dir = SD.open("/vendor");
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
                 // Apri e valida il file selezionato
                 String path = "/vendor/" + name;
                 File f = SD.open(path, FILE_READ);
                 if (!f) {
                     showMessage("Import vendor", "Cannot open file.");
                     return;
                 }
                 if (f.size() != 8) {
                     f.close();
                     showMessage("Import vendor", "File must be 8 bytes.");
                     return;
                 }

                 // Leggi i 4 byte di block18 e i 4 di block19
                 std::array<uint8_t, 4> ab18, ab19;
                 f.read(ab18.data(), 4);
                 f.read(ab19.data(), 4);
                 f.close();

                 // Sottomenu di conferma prima di scrivere
                 std::vector<Option> confirm = {
                     {"Yes, import",
                      [ab18, ab19]() {
                          drawMainBorderWithTitle("Import vendor");
                          setPadCursor(1, 2);
                          padprintln("Place tag on reader...");

                          // Ri-legge il tag per sicurezza prima di scrivere
                          if (!mikai_read_tag(&srixKey, &nfc)) {
                              showMessage("Import vendor", "Tag read failed.\nRetry.");
                              return;
                          }

                          // Verifica che il tag sia ancora in stato di reset
                          if (!mikai_is_reset(&srixKey)) {
                              showMessage("Import vendor", "Tag is no longer\nin reset state!");
                              return;
                          }

                          mikai_import_vendor(&srixKey, ab18.data(), ab19.data());

                          if (mikai_write_modified_blocks(&srixKey, &nfc) < 0) {
                              showMessage("Import vendor", "Write failed!\nRetry.");
                              return;
                          }

                          showMessage("Import vendor", "Done!\nNow use Add Credit\nto load credit.");
                      }, false},
                     {"Cancel", []() {}, false},
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU, "");
             },
             false}
        );
    }
    dir.close();

    if (fileOpts.empty()) {
        showMessage("Import vendor", "No .bin files\nin /vendor.");
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Select vendor file");
}
