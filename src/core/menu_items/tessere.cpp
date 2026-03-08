#include "Tessere.h"
#include "TessereLogic.h"
#include "core/display.h"

// ─── Menu opzioni ─────────────────────────────────────────────────────────────
void Tessere::optionsMenu() {
    std::vector<Option> options = {
        {"Info", []() { InfoTessera(); }, false},
        {"Read", []() { ReadTessera(); }, false},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere");
}

// ─── Icona ───────────────────────────────────────────────────────────────────
void Tessere::drawIcon(float scale) {
    clearIconArea();

    int w = (int)(scale * 80);
    int h = (int)(scale * 50);
    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;

    // Bordo carta
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // Chip EMV stilizzato
    int chipW = w / 4;
    int chipH = h / 2;
    int chipX = left + w / 8;
    int chipY = iconCenterY - chipH / 2;
    tft.drawRect(chipX, chipY, chipW, chipH, bruceConfig.priColor);
}
