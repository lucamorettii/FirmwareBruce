/*#include "Mikai.h"
#include "core/display.h"

void InfoSrix() {}

void Mikai::optionsMenu() {
    std::vector<Option> options;

    options.push_back({"Info", []() { InfoSrix(); }, false});
    options.push_back({"Read", []() {}, false});
    options.push_back({"Write", []() {}, false});
    options.push_back({"Add credit", []() {}, false});
    options.push_back({"Import vendor", []() {}, false});
    options.push_back({"Export vendor", []() {}, false});

    // Chiama loopOptions come per il menu principale, ma con tipo SUB
    loopOptions(options, MENU_TYPE_SUBMENU, "Mikai Menu");
}

void Mikai::drawIcon(float scale) {
    clearIconArea();

    int w = scale * 50; // larghezza distributore
    int h = scale * 80; // altezza distributore
    int padding = scale * 6;

    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;
    int right = iconCenterX + w / 2;
    int bottom = iconCenterY + h / 2;

    // Corpo esterno distributore
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // Area vetrina (parte superiore, vuota)
    int glassHeight = h * 0.55;
    tft.drawRect(left + padding, top + padding, w - 2 * padding, glassHeight - padding, bruceConfig.priColor);

    // Linee interne (ripiani snack minimal)
    int shelfY1 = top + padding + glassHeight / 3;
    int shelfY2 = top + padding + (glassHeight / 3) * 2;

    tft.drawLine(left + padding, shelfY1, right - padding, shelfY1, bruceConfig.priColor);
    tft.drawLine(left + padding, shelfY2, right - padding, shelfY2, bruceConfig.priColor);

    // Area inferiore (pannello comandi)
    int panelTop = top + glassHeight + padding;

    tft.drawRect(
        left + padding, panelTop, w - 2 * padding, h - glassHeight - 2 * padding, bruceConfig.priColor
    );

    // Pulsanti minimal (solo contorno)
    int btnSize = scale * 6;
    tft.drawRect(iconCenterX - btnSize / 2, panelTop + padding, btnSize, btnSize, bruceConfig.priColor);

    // Bocchetta erogazione
    int slotWidth = w / 3;
    int slotHeight = scale * 6;

    tft.drawRect(
        iconCenterX - slotWidth / 2,
        bottom - padding - slotHeight,
        slotWidth,
        slotHeight,
        bruceConfig.priColor
    );
}*/


/*
 * Mikai.cpp  â€“  Bruce firmware menu
 *
 * Drop MikaiLogic.h + MikaiLogic.cpp alongside this file.
 * Make sure pn532_srix.h / pn532_srix.cpp are already in your project.
 *
 * The PN532 instance (g_nfc) is initialised once on first use.
 * Adjust the I2C pins below if needed.
 */

#include "Mikai.h"
#include "core/display.h"
#include <Wire.h>

/* â”€â”€ PN532 instance â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * IRQ / RESET pins: pass 255 to use polling mode (no pin needed).
 * That is the safest default for Bruce with I2C.
 * â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static Arduino_PN532_SRIX g_nfc(255, 255);  // polling mode, no IRQ/RST pins
static bool g_nfc_ready = false;

/* â”€â”€ Mikai state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static struct srix_t  g_srix;
static struct mykey_t g_key = { &g_srix, 0 };
static bool           g_tag_loaded = false;

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 *  UI HELPERS
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/** Show a scrollable message and wait for any key. */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);
    padprintln(body);
    delay(300);
    keyStroke k;
    do { InputHandler(); delay(50); } while (!AnyKeyPress);
}

/** Show "Place tag..." screen and initialise PN532 + read tag. */
static bool loadTag() {
    // Init PN532 once
    if (!g_nfc_ready) {
        g_nfc_ready = g_nfc.init();
        if (!g_nfc_ready) {
            showMessage("Mikai", "PN532 init failed.\nCheck I2C wiring.");
            return false;
        }
    }

    // Reset state
    memset(&g_srix, 0, sizeof(g_srix));
    g_key.encryptionKey = 0;
    g_key.srix4k        = &g_srix;
    g_tag_loaded        = false;

    drawMainBorderWithTitle("Mikai");
    setPadCursor(1, 2);
    padprintln("Place MyKey on reader...");

    // mikai_nfc_init polls for the tag
    if (!mikai_nfc_init(&g_key, &g_nfc)) {
        showMessage("Mikai", "Tag read failed.");
        return false;
    }
    g_tag_loaded = true;
    return true;
}

static bool ensureTagLoaded() {
    if (g_tag_loaded) return true;
    return loadTag();
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 *  MENU ACTIONS
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/* â”€â”€ Info â”€â”€ */
static void actionInfo() {
    if (!ensureTagLoaded()) return;

    char buf[600];
    mikai_get_info_string(&g_key, buf, sizeof(buf));
    showMessage("Info", String(buf));
}

/* â”€â”€ Read (force fresh read) â”€â”€ */
static void actionRead() {
    g_tag_loaded = false;   // force re-read
    if (!loadTag()) return;

    char buf[80];
    uint16_t c = mikai_get_current_credit(&g_key);
    snprintf(buf, sizeof(buf),
             "Credit: %u.%02u EUR\n\nUID:\n%08X%08X",
             c/100, c%100,
             (uint32_t)(g_key.srix4k->uid >> 32),
             (uint32_t)(g_key.srix4k->uid));
    showMessage("Read", String(buf));
}

/* â”€â”€ Write â”€â”€ */
static void actionWrite() {
    if (!ensureTagLoaded()) return;
    if (!mikai_has_pending_writes(&g_key)) {
        showMessage("Write", "No pending changes.");
        return;
    }
    drawMainBorderWithTitle("Mikai");
    setPadCursor(1, 2);
    padprintln("Writing blocks...");
    padprintln("Keep tag on reader!");
    mikai_write_modified_blocks(&g_key, &g_nfc);
    showMessage("Write", "Done!");
}

/* â”€â”€ Add credit (amount selector like original mikai) â”€â”€ */
static void actionAddCredit() {
    if (!ensureTagLoaded()) return;

    // Amount selection menu â€“ same denominations as the original tool
    struct { const char *label; uint16_t cents; } amounts[] = {
        {"0.05 EUR",    5},
        {"0.10 EUR",   10},
        {"0.20 EUR",   20},
        {"0.50 EUR",   50},
        {"1.00 EUR",  100},
        {"2.00 EUR",  200},
        {"5.00 EUR",  500},
        {"10.00 EUR",1000},
        {"20.00 EUR",2000},
        {"50.00 EUR",5000},
    };
    const int N = sizeof(amounts)/sizeof(amounts[0]);

    std::vector<Option> opts;
    for (int i = 0; i < N; i++) {
        uint16_t c = amounts[i].cents;
        opts.push_back({amounts[i].label, [c]() {
            // Use Bruce's RTC if available, otherwise use a fixed date
            // Adjust this if Bruce exposes a date API differently
            uint8_t d = 1, m = 1, y = 25;   // fallback: 01/01/2025
#if defined(HAS_RTC)
            // If Bruce has RTC support:
            // d = rtc.getDay(); m = rtc.getMonth(); y = rtc.getYear() - 2000;
#endif
            int res = mikai_add_cents(&g_key, c, d, m, y);
            if (res == 0) {
                uint16_t newc = mikai_get_current_credit(&g_key);
                char buf[80];
                snprintf(buf, sizeof(buf),
                         "Added!\nNew credit:\n%u.%02u EUR\n\nPress Write to save.", newc/100, newc%100);
                showMessage("Add credit", String(buf));
            } else {
                char buf[40];
                snprintf(buf, sizeof(buf), "Error %d", res);
                showMessage("Add credit", String(buf));
            }
        }, false});
    }
    loopOptions(opts, MENU_TYPE_SUBMENU, "Add credit");
}

/* â”€â”€ Import vendor â”€â”€ */
static void actionImportVendor() {
    /*
     * In a full implementation you would load block18/block19 from a file
     * on the SD card. For now this shows how to call the function.
     *
     * Example: replace the bytes below with the real vendor blocks,
     * or add an SD file picker here.
     */
    if (!ensureTagLoaded()) return;

    showMessage("Import vendor",
        "Not yet implemented.\n\n"
        "To use: edit Mikai.cpp,\n"
        "replace the example bytes\n"
        "in actionImportVendor()\n"
        "with your vendor blocks.");

    // Example call (uncomment and fill real values):
    // uint8_t b18[4] = {0xXX, 0xXX, 0xXX, 0xXX};
    // uint8_t b19[4] = {0xXX, 0xXX, 0xXX, 0xXX};
    // mikai_import_vendor(&g_key, b18, b19);
    // showMessage("Import vendor", "Done! Press Write to save.");
}

static void actionExportVendor() {
    if (!ensureTagLoaded()) return;

    uint8_t buf[8];
    if (mikai_export_vendor(&g_key, buf) < 0) {
        showMessage("Export vendor", "Key is not bound\nto any vendor.");
        return;
    }
    char out[80];
    snprintf(out, sizeof(out),
             "Block 18:\n%02X %02X %02X %02X\n\nBlock 19:\n%02X %02X %02X %02X",
             buf[0], buf[1], buf[2], buf[3],
             buf[4], buf[5], buf[6], buf[7]);
    showMessage("Export vendor", String(out));
}


void Mikai::optionsMenu() {
    std::vector<Option> options = {
        {"Info",          []() { actionInfo();         }, false},
        {"Read",          []() { actionRead();         }, false},
        {"Write",         []() { actionWrite();        }, false},
        {"Add credit",    []() { actionAddCredit();    }, false},
        {"Import vendor", []() { actionImportVendor(); }, false},
        {"Export vendor", []() { actionExportVendor(); }, false},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Mikai");
}

void Mikai::drawIcon(float scale) {
    clearIconArea();

    int w       = (int)(scale * 50);
    int h       = (int)(scale * 80);
    int padding = (int)(scale * 6);
    int left    = iconCenterX - w / 2;
    int top     = iconCenterY - h / 2;
    int right   = iconCenterX + w / 2;
    int bottom  = iconCenterY + h / 2;

    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    int glassHeight = h * 55 / 100;
    tft.drawRect(left + padding, top + padding,
                 w - 2*padding, glassHeight - padding,
                 bruceConfig.priColor);

    int shelfY1 = top + padding + glassHeight / 3;
    int shelfY2 = top + padding + (glassHeight / 3) * 2;
    tft.drawLine(left+padding, shelfY1, right-padding, shelfY1, bruceConfig.priColor);
    tft.drawLine(left+padding, shelfY2, right-padding, shelfY2, bruceConfig.priColor);

    int panelTop = top + glassHeight + padding;
    tft.drawRect(left+padding, panelTop,
                 w - 2*padding, h - glassHeight - 2*padding,
                 bruceConfig.priColor);

    int btnSize = (int)(scale * 6);
    tft.drawRect(iconCenterX - btnSize/2, panelTop + padding,
                 btnSize, btnSize, bruceConfig.priColor);

    int slotW = w / 3;
    int slotH = (int)(scale * 6);
    tft.drawRect(iconCenterX - slotW/2, bottom - padding - slotH,
                 slotW, slotH, bruceConfig.priColor);
}
