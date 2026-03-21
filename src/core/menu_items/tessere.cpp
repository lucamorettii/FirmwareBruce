/**
 * @file Tessere.cpp
 * @brief Implementazione della classe Tessere: menu principale e icona per Bruce.
 *
 * Questo file gestisce:
 *   - La costruzione del menu di primo livello tramite loopOptions di Bruce.
 *   - Il disegno dell'icona vettoriale della carta con chip EMV nel launcher.
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunta la voce "Microel" che apre il sottomenu dedicato alle tessere
 *     Microel (Info, Read, Write con blocco 0).
 *
 * L'inizializzazione del PN532 è lazy: avviene dentro mifareInit(),
 * chiamata all'interno di ogni azione in TessereMenuLogic.cpp.
 */

#include "Tessere.h"
#include "core/display.h"
#include "itsFree/mifire/TessereMenuLogic.h" // Dichiara tutte le voci di menu

/**
 * @brief Mostra il menu principale Tessere con le sei operazioni disponibili.
 *
 * Ogni voce è una lambda che richiama la funzione corrispondente in
 * TessereMenuLogic.cpp. Il PN532 NON viene inizializzato qui: lo fa
 * la prima azione NFC invocata tramite mifareInit().
 *
 * Voci del menu:
 *   - Info       → identifica il tag (UID, SAK, ATQA, tipo, gestore)
 *   - Read       → legge il dump e lo salva su SD
 *   - Write      → scrive un dump scelto dalla SD
 *   - Auto Write → scrive automaticamente il dump in base all'UID
 *   - Microel    → sottomenu specifico per le tessere Microel (NUOVO)
 *   - Gestori    → gestisce la lista dei gestori (aggiungi/modifica/elimina)
 */
void Tessere::optionsMenu() {
    std::vector<Option> options = {
        // Voce Info: mostra UID, SAK, ATQA, tipo e gestore senza leggere i blocchi
        {"Info",       []() { InfoTessera(); },        false},

        // Voce Read: legge tutti i settori con le chiavi da /rfid/chiavi.txt e salva su SD
        {"Read",       []() { ReadTessera(); },        false},

        // Voce Write: scrive sul tag un dump .bin scelto dall'utente dalla SD
        {"Write",      []() { WriteTessera(); },       false},

        // Voce Auto Write: legge l'UID, cerca il dump corrispondente su SD e scrive automaticamente
        {"Auto Write", []() { AutoWriteTessera(); },   false},

        // Voce Microel: apre il sottomenu Microel (Info KDF, Read con KDF, Write con blocco 0)
        {"Microel",    []() { MicroelTessera(); },     false},

        // Voce Gestori: gestione lista gestori (solo SD, non richiede NFC)
        {"Gestori",    []() { GestoriMenuTessera(); }, false},
    };

    // Mostra il menu come sottomenu di Bruce con titolo "Tessere"
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere");
}

/**
 * @brief Disegna l'icona di una carta con chip EMV nel launcher di Bruce.
 *
 * L'icona rappresenta stilisticamente una carta di credito / badge NFC:
 *   - Un rettangolo orizzontale arrotondato (corpo della carta).
 *   - Un rettangolo più piccolo in alto a sinistra (chip EMV dorato).
 *
 * Tutte le dimensioni sono proporzionali al parametro @p scale, così
 * l'icona si adatta a qualsiasi risoluzione dello schermo di Bruce.
 *
 * @param scale Fattore di scala fornito da Bruce (tipicamente 0.5–2.0).
 */
void Tessere::drawIcon(float scale) {
    // Pulisce l'area dell'icona prima di disegnare
    clearIconArea();

    // ── Corpo della carta ─────────────────────────────────────────────────────
    int w = (int)(scale * 80);      // Larghezza proporzionale alla scala
    int h = (int)(scale * 50);      // Altezza proporzionale alla scala
    int left = iconCenterX - w / 2; // Bordo sinistro centrato
    int top = iconCenterY - h / 2;  // Bordo superiore centrato

    // Disegna il rettangolo esterno che rappresenta il corpo della carta
    tft.drawRect(left, top, w, h, bruceConfig.priColor);

    // ── Chip EMV ──────────────────────────────────────────────────────────────
    int chipW = w / 4;                   // Larghezza del chip = 1/4 della carta
    int chipH = h / 2;                   // Altezza del chip = 1/2 della carta
    int chipX = left + w / 8;            // Posizione X: margine a sinistra
    int chipY = iconCenterY - chipH / 2; // Posizione Y: centrato verticalmente

    // Disegna il rettangolo del chip EMV
    tft.drawRect(chipX, chipY, chipW, chipH, bruceConfig.priColor);
}
