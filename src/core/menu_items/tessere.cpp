#include "Tessere.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

static Adafruit_PN532 mifareNfc(255, 255);
static bool mifareNfcInit = false;

// Struttura per i dati dump
#define MIFARE_MAX_BLOCKS 256
struct MifareDump {
    uint8_t uid[7];
    uint8_t uidLen;
    uint8_t sak;
    uint16_t atqa;
    String tagType;
    uint8_t numSectors;
    uint8_t data[MIFARE_MAX_BLOCKS][16];
    bool blockRead[MIFARE_MAX_BLOCKS];
    uint8_t keyA[40][6];
    bool keyAFound[40];
    uint8_t keyB[40][6];
    bool keyBFound[40];
};

static MifareDump g_dump;

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

// Logica
static bool mifareInit() {
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

// Determina tipo tag dal SAK
static String getTagType(uint8_t sak) {
    if (sak == 0x08) return "Mifare 1K";
    if (sak == 0x18) return "Mifare 4K";
    if (sak == 0x09) return "Mifare Mini";
    if (sak == 0x00) return "Mifare UL";
    if (sak == 0x28) return "Mifare 1K (SmartMX)";
    if (sak == 0x38) return "Mifare 4K (SmartMX)";
    return "Unknown (SAK:" + String(sak, HEX) + ")";
}

// Numero settori dal tipo
static uint8_t getSectorCount(uint8_t sak) {
    if (sak == 0x18) return 40; // 4K
    if (sak == 0x09) return 5;  // Mini
    return 16;                  // 1K default
}

// Blocco trailer del settore
static uint8_t trailerBlock(uint8_t sector) {
    if (sector < 32) return sector * 4 + 3;
    return 32 * 4 + (sector - 32) * 16 + 15;
}

// Primo blocco del settore
static uint8_t firstBlock(uint8_t sector) {
    if (sector < 32) return sector * 4;
    return 32 * 4 + (sector - 32) * 16;
}

// Numero blocchi nel settore
static uint8_t blocksInSector(uint8_t sector) { return (sector < 32) ? 4 : 16; }

// Legge le chiavi dal file SD
static std::vector<std::array<uint8_t, 6>> loadKeysFromSD() {
    std::vector<std::array<uint8_t, 6>> keys;

    // Chiave di default sempre presente
    std::array<uint8_t, 6> defaultKey = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    keys.push_back(defaultKey);

    if (!SD.exists("/mifare/chiavi.txt")) return keys;

    File f = SD.open("/mifare/chiavi.txt", FILE_READ);
    if (!f) return keys;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() != 12) continue;

        std::array<uint8_t, 6> key;
        bool valid = true;
        for (int i = 0; i < 6; i++) {
            String byteStr = line.substring(i * 2, i * 2 + 2);
            for (char c : byteStr) {
                if (!isHexadecimalDigit(c)) {
                    valid = false;
                    break;
                }
            }
            if (!valid) break;
            key[i] = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
        }
        if (valid) keys.push_back(key);
    }
    f.close();
    return keys;
}

// Aspetta tag e legge UID
static bool waitForMifareTag() {
    drawMainBorderWithTitle("Mifare");
    setPadCursor(1, 2);
    padprintln("Place tag on reader...");

    uint8_t uid[7];
    uint8_t uidLen;

    if (mifareNfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 6000)) {
        memcpy(g_dump.uid, uid, uidLen);
        g_dump.uidLen = uidLen;
        // g_dump.sak = mifareNfc.getLastSAK();
        // g_dump.atqa = mifareNfc.getLastATQA();
        g_dump.tagType = getTagType(g_dump.sak);
        g_dump.numSectors = getSectorCount(g_dump.sak);
        return true;
    }

    showMessage("Mifare", "No tag found.");
    return false;
}

// Da qui in poi deve estare qua il codice
static void InfoTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    auto keys = loadKeysFromSD();

    char uidStr[20] = "";
    for (int i = 0; i < g_dump.uidLen; i++) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", g_dump.uid[i]);
        strcat(uidStr, tmp);
    }

    drawMainBorderWithTitle("Info");
    setPadCursor(1, 2);
    padprintln("UID: " + String(uidStr));

    char sakStr[5], atqaStr[5];
    snprintf(sakStr, sizeof(sakStr), "%02X", g_dump.sak);
    snprintf(atqaStr, sizeof(atqaStr), "%04X", g_dump.atqa);
    padprintln("SAK:  0x" + String(sakStr));
    padprintln("ATQA: 0x" + String(atqaStr));
    padprintln("Type: " + g_dump.tagType);

    delay(300);
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

static void ReadTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    auto keys = loadKeysFromSD();

    drawMainBorderWithTitle("Read");
    setPadCursor(1, 2);
    padprintln("Reading sectors...");

    memset(g_dump.blockRead, false, sizeof(g_dump.blockRead));
    memset(g_dump.keyAFound, false, sizeof(g_dump.keyAFound));

    for (uint8_t s = 0; s < g_dump.numSectors; s++) {
        uint8_t tb = trailerBlock(s);
        bool authed = false;

        for (auto &key : keys) {
            if (mifareNfc.mifareclassic_AuthenticateBlock(
                    g_dump.uid, g_dump.uidLen, tb, 0, (uint8_t *)key.data()
                )) {
                memcpy(g_dump.keyA[s], key.data(), 6);
                g_dump.keyAFound[s] = true;
                authed = true;
                break;
            }
        }

        if (!authed) continue;

        uint8_t fb = firstBlock(s);
        uint8_t nb = blocksInSector(s);
        for (uint8_t b = 0; b < nb; b++) {
            uint8_t blockNum = fb + b;
            if (mifareNfc.mifareclassic_ReadDataBlock(blockNum, g_dump.data[blockNum])) {
                g_dump.blockRead[blockNum] = true;
            }
        }
    }

    // Chiede il nome file
    String filename = keyboard("dump", 20, "File name:");
    if (filename == "") {
        showMessage("Read", "Cancelled.");
        return;
    }

    if (!SD.exists("/mifare")) SD.mkdir("/mifare");
    if (!SD.exists("/mifare/dump")) SD.mkdir("/mifare/dump");

    String path = "/mifare/dump/" + filename + ".dump";
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        showMessage("Read", "SD write failed.");
        return;
    }

    uint8_t totalBlocks = trailerBlock(g_dump.numSectors - 1) + 1;
    for (uint8_t b = 0; b < totalBlocks; b++) {
        if (g_dump.blockRead[b]) {
            f.write(g_dump.data[b], 16);
        } else {
            uint8_t empty[16];
            memset(empty, 0x00, 16);
            f.write(empty, 16);
        }
    }
    f.close();

    showMessage("Read", "Saved to:\n/mifare/dump/" + filename + ".dump");
}

static void WriteTessera() {
    if (!mifareInit()) return;

    if (!SD.exists("/mifare/dump")) {
        showMessage("Write", "No /mifare/dump\nfolder on SD.");
        return;
    }

    File dir = SD.open("/mifare/dump");
    std::vector<Option> fileOpts;

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        String name = String(entry.name());
        entry.close();
        if (!name.endsWith(".dump")) continue;

        fileOpts.push_back(
            {name.c_str(),
             [name]() {
                 String path = "/mifare/dump/" + name;
                 File f = SD.open(path, FILE_READ);
                 if (!f) {
                     showMessage("Write", "Cannot open file.");
                     return;
                 }

                 size_t sz = f.size();
                 uint8_t numSectors = 16;
                 if (sz == 1024) numSectors = 16;
                 else if (sz == 4096) numSectors = 40;
                 else if (sz == 320) numSectors = 5;

                 uint8_t dumpData[MIFARE_MAX_BLOCKS][16];
                 memset(dumpData, 0, sizeof(dumpData));
                 uint8_t totalBlocks = trailerBlock(numSectors - 1) + 1;
                 for (uint8_t b = 0; b < totalBlocks; b++) { f.read(dumpData[b], 16); }
                 f.close();

                 if (!waitForMifareTag()) return;

                 auto keys = loadKeysFromSD();

                 drawMainBorderWithTitle("Write");
                 setPadCursor(1, 2);
                 padprintln("Writing sectors...");
                 padprintln("Keep tag on reader!");

                 uint8_t ok = 0, fail = 0;
                 for (uint8_t s = 0; s < numSectors; s++) {
                     uint8_t tb = trailerBlock(s);
                     bool authed = false;

                     for (auto &key : keys) {
                         if (mifareNfc.mifareclassic_AuthenticateBlock(
                                 g_dump.uid, g_dump.uidLen, tb, 0, (uint8_t *)key.data()
                             )) {
                             authed = true;
                             break;
                         }
                     }

                     if (!authed) {
                         fail++;
                         continue;
                     }

                     uint8_t fb = firstBlock(s);
                     uint8_t nb = blocksInSector(s);
                     for (uint8_t b = 0; b < nb; b++) {
                         uint8_t blockNum = fb + b;
                         if (s == 0 && b == 0) continue; // blocco manifattura readonly
                         if (mifareNfc.mifareclassic_WriteDataBlock(blockNum, dumpData[blockNum])) {
                             ok++;
                         } else {
                             fail++;
                         }
                     }
                 }

                 char msg[60];
                 snprintf(msg, sizeof(msg), "Done!\nOK: %u blocks\nFail: %u blocks", ok, fail);
                 showMessage("Write", String(msg));
             },
             false}
        );
    }
    dir.close();

    if (fileOpts.empty()) {
        showMessage("Write", "No .dump files\nin /mifare/dump.");
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Select dump file");
}

void Tessere::optionsMenu() {
    std::vector<Option> options = {
        {"Info",  []() { InfoTessera(); },  false},
        {"Read",  []() { ReadTessera(); },  false}, //  Da vedere
        {"Write", []() { WriteTessera(); }, false}, //  Da vedere
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere");
}

void Tessere::drawIcon(float scale) {
    clearIconArea();

    int w = (int)(scale * 80);
    int h = (int)(scale * 50);

    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;

    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    int chipW = w / 4;
    int chipH = h / 2;
    int chipX = left + w / 8;
    int chipY = iconCenterY - chipH / 2;

    tft.drawRect(chipX, chipY, chipW, chipH, bruceConfig.priColor);
}
