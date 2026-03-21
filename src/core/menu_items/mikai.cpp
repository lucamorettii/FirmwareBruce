/**
 * @file Mikai.cpp
 * @brief Implementazione della classe Mikai: menu principale e icona per Bruce.
 *
 * Questo file gestisce:
 *   - L'inizializzazione lazy del driver PN532 (solo al primo accesso al menu).
 *   - La costruzione del menu di primo livello tramite loopOptions di Bruce.
 *   - Il disegno dell'icona vettoriale del lettore NFC nel launcher.
 */

#include "Mikai.h"
#include "core/display.h"
#include "itsfree/mikai/MikaiMenuLogic.h"
#include <Wire.h>

/// Flag statico: true dopo la prima inizializzazione riuscita del PN532.
static bool nfcIsInit = false;

/**
 * @brief Inizializza il PN532 (se necessario) e mostra il menu principale Mikai.
 *
 * L'inizializzazione I²C avviene una sola volta per sessione: i pin SDA/SCL
 * vengono letti dalla configurazione di Bruce (bruceConfigPins) e il bus viene
 * avviato a 100 kHz. Se il PN532 non risponde, viene mostrato un errore e
 * la funzione ritorna senza aprire il menu.
 */
void Mikai::optionsMenu() {
    // Inizializzazione lazy: eseguita solo alla prima chiamata
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

    // Costruisce e visualizza il menu principale con le operazioni disponibili
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

/**
 * @brief Disegna l'icona del lettore di carte NFC nel launcher di Bruce.
 *
 * L'icona rappresenta stilisticamente un terminale NFC / lettore di carte:
 *   - Un rettangolo esterno (corpo del dispositivo).
 *   - Un rettangolo interno in alto (schermo / zona NFC).
 *   - Un rettangolo interno in basso (pannello pulsanti).
 *   - Un pulsante quadrato al centro del pannello.
 *   - Uno slot rettangolare in basso (slot carta).
 *
 * Tutte le dimensioni sono calcolate in proporzione al parametro @p scale
 * fornito da Bruce, in modo che l'icona si adatti a qualsiasi risoluzione.
 *
 * @param scale Fattore di scala (tipicamente compreso tra 0.5 e 2.0).
 */
void Mikai::drawIcon(float scale) {
    clearIconArea();

    // Dimensioni e posizione del corpo principale
    int w = (int)(scale * 50);
    int h = (int)(scale * 80);
    int padding = (int)(scale * 6);
    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;
    int right = iconCenterX + w / 2;
    int bottom = iconCenterY + h / 2;

    // Rettangolo esterno: corpo del dispositivo
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // Rettangolo superiore: schermo / zona di lettura NFC
    int glassHeight = h * 55 / 100;
    tft.drawRect(left + padding, top + padding, w - 2 * padding, glassHeight - padding, bruceConfig.priColor);

    // Rettangolo inferiore: pannello dei pulsanti
    int panelTop = top + glassHeight + padding;
    tft.drawRect(
        left + padding, panelTop, w - 2 * padding, h - glassHeight - 2 * padding, bruceConfig.priColor
    );

    // Pulsante quadrato centrale nel pannello
    int btnSize = (int)(scale * 6);
    tft.drawRect(iconCenterX - btnSize / 2, panelTop + padding, btnSize, btnSize, bruceConfig.priColor);

    // Slot carta nella parte inferiore del corpo
    int slotW = w / 3;
    int slotH = (int)(scale * 6);
    tft.drawRect(iconCenterX - slotW / 2, bottom - padding - slotH, slotW, slotH, bruceConfig.priColor);
}
