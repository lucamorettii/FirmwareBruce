#include "Mikai.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Wire.h>

static Arduino_PN532_SRIX nfc(255, 255); // polling mode, no IRQ/RST pins
static bool nfcIsInit = false;           // Bool per sapere se è gia stato inizializzato ed è tutto ok

// Variabili per le funzionalità SRIX mikai
static struct srix_t srix;
static struct mykey_t srixKey = {&srix, 0};

// Funzione per visualizzare titolo e messaggio
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);
    padprintln(body);
    delay(300);
    keyStroke k;
    do {
        InputHandler();
        delay(50);
    } while (!AnyKeyPress);
}

// Leggo il tag srix
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

// AZIONI MENU
// Mostra info del tag
static void actionInfo() {
    if (!loadTag()) return;
    char buf[800];
    mikai_get_info_string(&srixKey, buf, sizeof(buf));

    drawMainBorderWithTitle("Info");
    setPadCursor(1, 2);

    // Stampa riga per riga
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

// Scrive da RAM a Key
static void actionWrite() {
    if (!mikai_has_pending_writes(&srixKey)) {
        showMessage("Write", "No pending changes.");
        return;
    }
    drawMainBorderWithTitle("Mikai");
    setPadCursor(1, 2);
    padprintln("Writing blocks...");
    padprintln("Keep tag on reader!");
    mikai_write_modified_blocks(&srixKey, &nfc);
    showMessage("Write", "Done!");
}

// Scrivo il nuovo credito in RAM e poi se vuoi puoi scriverlo
static void actionAddCredit() {
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
                 uint8_t d = 1, m = 1, y = 26; // Data iniziale
                 int res = mikai_add_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[40];
                     snprintf(buf, sizeof(buf), "New credit: %u.%02u EUR", newc / 100, newc % 100);

                     std::vector<Option> confirm = {
                         {"Write to tag", []() { actionWrite(); }, false},
                         {"Cancel",       []() {},                 false},
                     };
                     loopOptions(confirm, MENU_TYPE_SUBMENU, String(buf).c_str());
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

static void actionExportVendor() {
    if (!loadTag()) return;

    uint8_t buf[8];
    if (mikai_export_vendor(&srixKey, buf) < 0) {
        showMessage("Export vendor", "Key is not bound to any vendor.");
        return;
    }

    String filename = keyboard("vendor", 20, "File name:");
    if (filename == "") {
        showMessage("Export vendor", "Cancelled.");
        return;
    }

    // Crea la cartella se non esiste
    if (!SD.exists("/vendor")) { SD.mkdir("/vendor"); }

    String path = "/vendor/mikai_" + filename + ".bin";
    File f = SD.open(path, FILE_WRITE);

    if (!f) {
        showMessage("Export vendor", "SD write failed.");
        return;
    }
    f.write(buf, 8);
    f.close();

    showMessage("Export vendor", "Saved to: " + path);
}

/*
static void actionImportVendor() {
    if (!loadTag()) return;

    showMessage(
        "Import vendor",
        "Not yet implemented.\n\n"
        "To use: edit Mikai.cpp,\n"
        "replace the example bytes\n"
        "in actionImportVendor()\n"
        "with your vendor blocks."
    );

    // Example call (uncomment and fill real values):
    // uint8_t b18[4] = {0xXX, 0xXX, 0xXX, 0xXX};
    // uint8_t b19[4] = {0xXX, 0xXX, 0xXX, 0xXX};
    // mikai_import_vendor(&srixKey, b18, b19);
    // showMessage("Import vendor", "Done! Press Write to save.");
}
*/

void Mikai::optionsMenu() {
    // Inizializzo il PN532
    if (!nfcIsInit) {
        int sda_pin = bruceConfigPins.i2c_bus.sda;
        int scl_pin = bruceConfigPins.i2c_bus.scl;
        Wire.begin(sda_pin, scl_pin);
        Wire.setClock(100000);

        nfcIsInit = nfc.init();
        if (!nfcIsInit) {
            displayError("PN532 init failed.", true);
            return;
        }
    }

    std::vector<Option> options = {
        {"Info",          []() { actionInfo(); },         false},
        {"Add credit",    []() { actionAddCredit(); },    false},
        {"Export vendor", []() { actionExportVendor(); }, false},
        // {"Import vendor", []() { actionImportVendor(); }, false},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Mikai");
}

void Mikai::drawIcon(float scale) {
    clearIconArea();

    int w = (int)(scale * 50);
    int h = (int)(scale * 80);
    int padding = (int)(scale * 6);
    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;
    int right = iconCenterX + w / 2;
    int bottom = iconCenterY + h / 2;

    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    int glassHeight = h * 55 / 100;
    tft.drawRect(left + padding, top + padding, w - 2 * padding, glassHeight - padding, bruceConfig.priColor);

    int panelTop = top + glassHeight + padding;
    tft.drawRect(
        left + padding, panelTop, w - 2 * padding, h - glassHeight - 2 * padding, bruceConfig.priColor
    );

    int btnSize = (int)(scale * 6);
    tft.drawRect(iconCenterX - btnSize / 2, panelTop + padding, btnSize, btnSize, bruceConfig.priColor);

    int slotW = w / 3;
    int slotH = (int)(scale * 6);
    tft.drawRect(iconCenterX - slotW / 2, bottom - padding - slotH, slotW, slotH, bruceConfig.priColor);
}
