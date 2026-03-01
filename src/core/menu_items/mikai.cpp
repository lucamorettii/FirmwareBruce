#include "Mikai.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Wire.h>

static Arduino_PN532_SRIX nfc(255, 255);
static bool nfcIsInit = false;

static struct srix_t srix;
static struct mykey_t srixKey = {&srix, 0};

// Funzione per visualizzare titolo e messaggio con supporto \n
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    String tmp = body;
    int start = 0;
    int idx;
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

// Funzione per leggere il dump nelle voci del menu
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

static void actionInfo() {
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

static void actionWrite() {
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
                 uint8_t d = rtc.getDay();
                 uint8_t m = rtc.getMonth();
                 uint8_t y = rtc.getYear() - 2000;

                 int res = mikai_add_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[40];
                     snprintf(buf, sizeof(buf), "New credit: %u.%02u EUR", newc / 100, newc % 100);

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

static void actionSetCredit() {
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
                 uint8_t d = 1;
                 uint8_t m = 1;
                 uint8_t y = 2026;

                 int res = mikai_set_cents(&srixKey, c, d, m, y);
                 if (res == 0) {
                     uint16_t newc = mikai_get_current_credit(&srixKey);
                     char buf[40];
                     snprintf(buf, sizeof(buf), "Credit set: %u.%02u EUR", newc / 100, newc % 100);

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

static void actionExportVendor() {
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

    if (!SD.exists("/vendor")) { SD.mkdir("/vendor"); }

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

static void actionReset() {
    if (!loadTag()) return;

    mikai_reset_key(&srixKey);

    std::vector<Option> confirm = {
        {"Write to tag", []() { actionWrite(); }, false},
        {"Cancel",       []() {},                 false},
    };
    loopOptions(confirm, MENU_TYPE_SUBMENU, "Write reset?");
}

static void actionImportVendor() {
    if (!loadTag()) return;

    if (!mikai_is_reset(&srixKey)) {
        showMessage("Import vendor", "Key is already bound!\nDo a Reset first,\nthen import vendor.");
        return;
    }

    if (!SD.exists("/vendor")) {
        showMessage("Import vendor", "No /vendor folder on SD.");
        return;
    }

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

                 uint8_t b18[4], b19[4];
                 f.read(b18, 4);
                 f.read(b19, 4);
                 f.close();

                 char preview[80];
                 snprintf(
                     preview,
                     sizeof(preview),
                     "B18: %02X %02X %02X %02X\nB19: %02X %02X %02X %02X\nConfirm?",
                     b18[0],
                     b18[1],
                     b18[2],
                     b18[3],
                     b19[0],
                     b19[1],
                     b19[2],
                     b19[3]
                 );

                 std::vector<Option> confirm = {
                     {"Yes, import",
                      [b18, b19]() mutable {
                          mikai_import_vendor(&srixKey, b18, b19);

                          uint16_t c = mikai_get_current_credit(&srixKey);
                          char msg[60];
                          snprintf(
                              msg,
                              sizeof(msg),
                              "Done!\nNew SK: %08lX\nCredit: %u.%02u EUR",
                              srixKey.encryptionKey,
                              c / 100,
                              c % 100
                          );

                          std::vector<Option> writeOpts = {
                              {"Write to tag", []() { actionWrite(); }, false},
                              {"Cancel", []() {}, false},
                          };
                          loopOptions(writeOpts, MENU_TYPE_SUBMENU, msg);
                      }, false},
                     {"Cancel", []() {}, false},
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU, preview);
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

void Mikai::optionsMenu() {
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
        {"Set credit",    []() { actionSetCredit(); },    false},
        {"Reset",         []() { actionReset(); },        false},
        {"Export vendor", []() { actionExportVendor(); }, false},
        {"Import vendor", []() { actionImportVendor(); }, false},
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
