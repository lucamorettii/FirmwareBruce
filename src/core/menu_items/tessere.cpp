/**
 * @file Tessere.cpp
 * @brief Implementazione della classe Tessere: menu principale e icona per Bruce.
 *
 * Questo file gestisce:
 *   - La costruzione del menu di primo livello tramite loopOptions di Bruce,
 *     con le cinque azioni disponibili (Info, Read, Write, Auto Write, Gestori).
 *   - Il disegno dell'icona vettoriale della carta con chip EMV nel launcher.
 *
 * L'inizializzazione del PN532 è lazy e avviene dentro mifareInit(),
 * chiamata all'interno di ogni azione in TessereMenuLogic.cpp.
 */

#include "Tessere.h"
#include "TessereMenuLogic.h"
#include "core/display.h"

/**
 * @brief Mostra il menu principale Tessere con le cinque operazioni disponibili.
 *
 * Le azioni sono collegate tramite lambda alle funzioni dichiarate in
 * TessereMenuLogic.h. Il PN532 viene inizializzato internamente dalla
 * prima azione NFC invocata, non qui.
 */
void Tessere::optionsMenu() {
    std::vector<Option> options = {
        {"Info",       []() { InfoTessera(); },      false},
        {"Read",       []() { ReadTessera(); },      false},
        {"Write",      []() { WriteTessera(); },     false},
        {"Auto Write", []() { AutoWriteTessera(); }, false},
        {"Gestori",    []() { GestoriMenu(); },      false},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere");
}

/**
 * @brief Disegna l'icona di una carta con chip EMV nel launcher di Bruce.
 *
 * L'icona rappresenta stilisticamente una carta di credito / badge NFC:
 *   - Un rettangolo orizzontale (corpo della carta).
 *   - Un rettangolo più piccolo in alto a sinistra (chip EMV dorato).
 *
 * Tutte le dimensioni sono proporzionali al parametro @p scale, in modo
 * che l'icona si adatti a qualsiasi risoluzione dello schermo.
 *
 * @param scale Fattore di scala fornito da Bruce (tipicamente 0.5–2.0).
 */
void Tessere::drawIcon(float scale) {
    clearIconArea();

    // Dimensioni e posizione del corpo della carta
    int w = (int)(scale * 80);
    int h = (int)(scale * 50);
    int left = iconCenterX - w / 2;
    int top = iconCenterY - h / 2;

    // Rettangolo esterno: bordo della carta
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // Chip EMV: rettangolo più piccolo in alto a sinistra
    int chipW = w / 4;
    int chipH = h / 2;
    int chipX = left + w / 8;
    int chipY = iconCenterY - chipH / 2;
    tft.drawRect(chipX, chipY, chipW, chipH, bruceConfig.priColor);
}
