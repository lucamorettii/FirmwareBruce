#include "Mikai.h"
#include "MikaiMenuLogic.h"
#include "core/display.h"
#include <Wire.h>

static bool nfcIsInit = false;

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
