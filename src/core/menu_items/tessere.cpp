#include "Tessere.h"
#include "core/display.h"

void InfoTessera() {}

void Tessere::optionsMenu() {
    std::vector<Option> options;

    options.push_back({"Info", []() { InfoTessera(); }, false});
    options.push_back({"Read", []() {}, false});
    options.push_back({"Write", []() {}, false});

    // Chiama loopOptions come per il menu principale, ma con tipo SUB
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere Menu");
}
void Tessere::drawIcon(float scale) {
    clearIconArea();

    int s = (int)scale; // forza intero per evitare artefatti

    int w = s * 80; // larghezza carta
    int h = s * 50; // altezza carta

    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;

    // Disegna carta
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // Chip (centrato verticalmente, lato sinistro)
    int chipW = w / 4;
    int chipH = h / 2;

    int chipX = left + w / 8;
    int chipY = iconCenterY - chipH / 2;

    tft.drawRect(chipX, chipY, chipW, chipH, bruceConfig.priColor);
}
