#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

// ─── Stato del modulo ─────────────────────────────────────────────────────────
static Adafruit_PN532 mifareNfc(255, 255);
static bool mifareNfcInit = false;

MifareDump g_dump; // definizione della variabile esterna dichiarata nel .h

// ─── Helper UI ───────────────────────────────────────────────────────────────
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

// ─── Helper tag ──────────────────────────────────────────────────────────────
String getTagType(uint8_t sak) {
    if (sak == 0x08) return "Mifare 1K";
    if (sak == 0x18) return "Mifare 4K";
    if (sak == 0x09) return "Mifare Mini";
    if (sak == 0x00) return "Mifare UL";
    if (sak == 0x28) return "Mifare 1K (SmartMX)";
    if (sak == 0x38) return "Mifare 4K (SmartMX)";
    return "Unknown (SAK:" + String(sak, HEX) + ")";
}

uint8_t getSectorCount(uint8_t sak) {
    if (sak == 0x18) return 40; // 4K
    if (sak == 0x09) return 5;  // Mini
    return 16;                  // 1K default
}

uint8_t trailerBlock(uint8_t sector) {
    if (sector < 32) return sector * 4 + 3;
    return 32 * 4 + (sector - 32) * 16 + 15;
}

uint8_t firstBlock(uint8_t sector) {
    if (sector < 32) return sector * 4;
    return 32 * 4 + (sector - 32) * 16;
}

uint8_t blocksInSector(uint8_t sector) { return (sector < 32) ? 4 : 16; }

// ─── Chiavi SD ───────────────────────────────────────────────────────────────
std::vector<std::array<uint8_t, 6>> loadKeysFromSD() {
    std::vector<std::array<uint8_t, 6>> keys;

    // Chiave di default sempre presente
    keys.push_back({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

    if (!SD.exists("/mifare/chiavi.txt")) return keys;
    File f = SD.open("/mifare/chiavi.txt", FILE_READ);
    if (!f) return keys;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() != 12) continue;

        std::array<uint8_t, 6> key;
        bool valid = true;
        for (int i = 0; i < 6 && valid; i++) {
            String byteStr = line.substring(i * 2, i * 2 + 2);
            for (char c : byteStr) {
                if (!isHexadecimalDigit(c)) {
                    valid = false;
                    break;
                }
            }
            if (valid) key[i] = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
        }
        if (valid) keys.push_back(key);
    }
    f.close();
    return keys;
}

// ─── Inizializzazione PN532 ──────────────────────────────────────────────────
bool mifareInit() {
    if (!mifareNfcInit) {
        int sda_pin = bruceConfigPins.i2c_bus.sda;
        int scl_pin = bruceConfigPins.i2c_bus.scl;
        Wire.begin(sda_pin, scl_pin);
        Wire.setClock(100000);
        mifareNfc.begin();
        mifareNfcInit = mifareNfc.getFirmwareVersion() != 0;
        if (!mifareNfcInit) {
            displayError("PN532 init failed.", true);
            return false;
        }
        mifareNfc.SAMConfig();
    }
    return true;
}

// ─── Attesa tag ───────────────────────────────────────────────────────────────
bool waitForMifareTag() {
    drawMainBorderWithTitle("Mifare");
    setPadCursor(1, 2);
    padprintln("Place tag on reader...");

    uint8_t uid[7], uidLen;
    if (mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 6000)) {
        memcpy(g_dump.uid, uid, uidLen);
        g_dump.uidLen = uidLen;
        g_dump.sak = mifareNfc.getLastSAK();
        g_dump.atqa = mifareNfc.getLastATQA();
        g_dump.tagType = getTagType(g_dump.sak);
        g_dump.numSectors = getSectorCount(g_dump.sak);
        return true;
    }

    showMessage("Mifare", "No tag found.");
    return false;
}

// ─── Info ─────────────────────────────────────────────────────────────────────
void InfoTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    // Formatta UID come stringa esadecimale
    char uidStr[20] = "";
    for (int i = 0; i < g_dump.uidLen; i++) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", g_dump.uid[i]);
        strcat(uidStr, tmp);
    }

    char sakStr[5], atqaStr[5];
    snprintf(sakStr, sizeof(sakStr), "%02X", g_dump.sak);
    snprintf(atqaStr, sizeof(atqaStr), "%04X", g_dump.atqa);

    drawMainBorderWithTitle("Info");
    setPadCursor(1, 2);
    padprintln("UID:  " + String(uidStr));
    padprintln("SAK:  0x" + String(sakStr));
    padprintln("ATQA: 0x" + String(atqaStr));
    padprintln("Type: " + g_dump.tagType);

    delay(300);
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

// ─── Read + salvataggio dump ─────────────────────────────────────────────────
void ReadTessera() {}
