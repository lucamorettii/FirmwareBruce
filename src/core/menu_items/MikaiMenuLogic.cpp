#include "MikaiMenuLogic.h"
#include "MikaiLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <SD.h>

// ─── Helper UI ───────────────────────────────────────────────────────────────
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
                 uint8_t d = 1;
                 uint8_t m = 1;
                 uint8_t y = 26;

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
                 uint8_t y = 26;

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

void actionReset() {
    if (!loadTag()) return;

    mikai_reset_key(&srixKey);

    std::vector<Option> confirm = {
        {"Write to tag", []() { actionWrite(); }, false},
        {"Cancel",       []() {},                 false},
    };
    loopOptions(confirm, MENU_TYPE_SUBMENU, "Write reset?");
}

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

                 std::array<uint8_t, 4> ab18, ab19;
                 f.read(ab18.data(), 4);
                 f.read(ab19.data(), 4);
                 f.close();

                 std::vector<Option> confirm = {
                     {"Yes, import",
                      [ab18, ab19]() { // rimosso mutable, non serve
                          drawMainBorderWithTitle("Import vendor");
                          setPadCursor(1, 2);
                          padprintln("Place tag on reader...");

                          if (!mikai_read_tag(&srixKey, &nfc)) {
                              showMessage("Import vendor", "Tag read failed.\nRetry.");
                              return;
                          }

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
