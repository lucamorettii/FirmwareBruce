#include "MikaiMenu.h"
#include "MikaiLogica.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <SD.h>

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

static bool loadTag() {
    drawMainBorderWithTitle("Mikai");
    setPadCursor(1, 2);
    padprintln("Posiziona MyKey sul lettore...");

    if (!mikai_read_tag(&srixKey, &nfc)) {
        showMessage("Mikai", "Lettura tag fallita.");
        return false;
    }
    return true;
}

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

void actionWrite() {
    if (!mikai_has_pending_writes(&srixKey)) {
        showMessage("Scrittura", "Nessuna modifica in sospeso.");
        return;
    }

    drawMainBorderWithTitle("Scrittura");
    setPadCursor(1, 2);
    padprintln("Scrittura blocchi in corso...");
    padprintln("Mantieni il tag sul lettore!");

    mikai_write_modified_blocks(&srixKey, &nfc);
    showMessage("Scrittura", "Operazione completata!");
}

void actionAddCredit() {
    if (!loadTag()) return;

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
                 uint8_t d = 1, m = 1, y = 26;
                 int res = mikai_add_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[48];
                     snprintf(buf, sizeof(buf), "Nuovo credito: %u.%02u EUR", newc / 100, newc % 100);
                     std::vector<Option> confirm = {
                         {"Scrivi sul tag", []() { actionWrite(); }, false},
                         {"Annulla",        []() {},                 false},
                     };
                     loopOptions(confirm, MENU_TYPE_SUBMENU, buf);
                 } else {
                     char buf[32];
                     snprintf(buf, sizeof(buf), "Errore %d", res);
                     showMessage("Aggiungi credito", String(buf));
                 }
             },
             false}
        );
    }
    loopOptions(opts, MENU_TYPE_SUBMENU, "Aggiungi credito");
}

void actionSetCredit() {
    if (!loadTag()) return;

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
                 uint8_t d = 1, m = 1, y = 26;
                 int res = mikai_set_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[48];
                     snprintf(buf, sizeof(buf), "Credito impostato: %u.%02u EUR", newc / 100, newc % 100);
                     std::vector<Option> confirm = {
                         {"Scrivi sul tag", []() { actionWrite(); }, false},
                         {"Annulla",        []() {},                 false},
                     };
                     loopOptions(confirm, MENU_TYPE_SUBMENU, buf);
                 } else {
                     char buf[32];
                     snprintf(buf, sizeof(buf), "Errore %d", res);
                     showMessage("Imposta credito", String(buf));
                 }
             },
             false}
        );
    }
    loopOptions(opts, MENU_TYPE_SUBMENU, "Imposta credito");
}

void actionExportVendor() {
    if (!loadTag()) return;

    uint8_t buf[8];
    if (mikai_export_vendor(&srixKey, buf) < 0) {
        showMessage("Esporta vendor", "La chiave non e' associata\na nessun vendor.");
        return;
    }

    String filename = keyboard("vendor", 20, "Nome file:");
    if (filename == "") {
        showMessage("Esporta vendor", "Annullato.");
        return;
    }

    if (!SD.exists("/rfid")) SD.mkdir("/rfid");
    if (!SD.exists("/rfid/mikai")) SD.mkdir("/rfid/mikai");
    if (!SD.exists("/rfid/mikai/vendor")) SD.mkdir("/rfid/mikai/vendor");

    String path = "/rfid/mikai/vendor/mikai_" + filename + ".bin";
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        showMessage("Esporta vendor", "Scrittura SD fallita.");
        return;
    }
    f.write(buf, 8);
    f.close();

    showMessage("Esporta vendor", "Salvato in:\n" + path);
}

void actionReset() {
    if (!loadTag()) return;

    mikai_reset_key(&srixKey);

    std::vector<Option> confirm = {
        {"Scrivi sul tag", []() { actionWrite(); }, false},
        {"Annulla",        []() {},                 false},
    };
    loopOptions(confirm, MENU_TYPE_SUBMENU, "Scrivi reset?");
}

void actionImportVendor() {
    if (!loadTag()) return;

    if (!mikai_is_reset(&srixKey)) {
        showMessage("Importa vendor", "La chiave e' gia' associata!\nEsegui prima un Reset,\npoi importa il vendor.");
        return;
    }

    if (!SD.exists("/rfid/mikai/vendor")) {
        showMessage("Importa vendor", "Nessuna cartella\n/rfid/mikai/vendor sulla SD.");
        return;
    }

    File dir = SD.open("/rfid/mikai/vendor");
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
                 String path = "/rfid/mikai/vendor/" + name;
                 File f = SD.open(path, FILE_READ);
                 if (!f) {
                     showMessage("Importa vendor", "Impossibile aprire il file.");
                     return;
                 }
                 if (f.size() != 8) {
                     f.close();
                     showMessage("Importa vendor", "Il file deve essere di 8 byte.");
                     return;
                 }

                 std::array<uint8_t, 4> ab18, ab19;
                 f.read(ab18.data(), 4);
                 f.read(ab19.data(), 4);
                 f.close();

                 std::vector<Option> confirm = {
                     {"Si, importa",
                      [ab18, ab19]() {
                          drawMainBorderWithTitle("Importa vendor");
                          setPadCursor(1, 2);
                          padprintln("Posiziona il tag sul lettore...");

                          if (!mikai_read_tag(&srixKey, &nfc)) {
                              showMessage("Importa vendor", "Lettura tag fallita.\nRiprova.");
                              return;
                          }

                          if (!mikai_is_reset(&srixKey)) {
                              showMessage("Importa vendor", "Il tag non e' piu'\nin stato di reset!");
                              return;
                          }

                          mikai_import_vendor(&srixKey, ab18.data(), ab19.data());

                          if (mikai_write_modified_blocks(&srixKey, &nfc) < 0) {
                              showMessage("Importa vendor", "Scrittura fallita!\nRiprova.");
                              return;
                          }

                          showMessage("Importa vendor", "Operazione completata!\nUsa Aggiungi credito\nper caricare il credito.");
                      }, false},
                     {"Annulla", []() {}, false},
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU, "");
             },
             false}
        );
    }
    dir.close();

    if (fileOpts.empty()) {
        showMessage("Importa vendor", "Nessun file .bin in\n/rfid/mikai/vendor.");
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Seleziona file vendor");
}

void actionDump() {
    if (!loadTag()) return;

    uint64_t uid;
    uint8_t eeprom[SRIX4K_BYTES];
    mikai_export_dump(&srixKey, &uid, eeprom);

    if (!SD.exists("/rfid")) SD.mkdir("/rfid");
    if (!SD.exists("/rfid/mikai")) SD.mkdir("/rfid/mikai");
    if (!SD.exists("/rfid/mikai/dump")) SD.mkdir("/rfid/mikai/dump");

    char fname[32];
    snprintf(fname, sizeof(fname), "%016llX.bin", (unsigned long long)uid);
    String path = "/rfid/mikai/dump/" + String(fname);

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        showMessage("Dump", "Scrittura SD fallita.");
        return;
    }
    f.write((uint8_t *)&uid, sizeof(uid));
    f.write(eeprom, SRIX4K_BYTES);
    f.close();

    showMessage("Dump", "Salvato in:\n" + path);
}
