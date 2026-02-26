#include "Mikai.h"
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
}
