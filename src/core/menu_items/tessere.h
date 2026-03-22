#pragma once

/**
 * @file Tessere.h
 * @brief Dichiarazione della classe Tessere, voce di menu Bruce per tag MIFARE.
 *
 * Tessere implementa MenuItemInterface di Bruce aggiungendo al firmware
 * la gestione dei tag NFC MIFARE (Classic 1K/4K/Mini, Ultralight).
 *
 * Il PN532 viene inizializzato una sola volta all'ingresso del menu
 * (in optionsMenu()), non ad ogni singola operazione.
 *
 * Struttura del menu:
 *   - Info           → UID, SAK, ATQA, tipo, gestore, dump SD
 *   - Read           → legge dump e salva su SD
 *   - Write          → sottomenu: Scrivi / Scrivi Auto
 *   - Microel        → sottomenu: Info, Leggi, Imposta Credito, Genera Chiavi
 *   - Config         → sottomenu: Gestori (Visualizza/Aggiungi/Modifica/Elimina)
 */

#ifndef __TESSERE_H__
#define __TESSERE_H__

#include <MenuItemInterface.h>
#include <vector>

/**
 * @brief Voce di menu Bruce per la gestione dei tag NFC MIFARE.
 *
 * Eredita da MenuItemInterface e sovrascrive i metodi richiesti:
 *   - drawIcon()    → icona carta con chip EMV nel launcher.
 *   - optionsMenu() → menu principale con inizializzazione PN532.
 *   - hasTheme() / themePath() → integrazione con il sistema di theming.
 */
class Tessere : public MenuItemInterface {
public:
    /// Costruisce la voce con il nome "Tessere" visibile nel launcher di Bruce.
    Tessere() : MenuItemInterface("Tessere") {}

    /**
     * @brief Disegna l'icona della carta con chip EMV nel launcher di Bruce.
     * @param scala Fattore di scala fornito da Bruce in base alla risoluzione.
     */
    void drawIcon(float scala) override;

    /**
     * @brief Inizializza il PN532 e mostra il menu principale delle operazioni.
     *
     * Se il PN532 non risponde, mostra un errore e ritorna senza aprire il menu.
     * Il PN532 non viene reinizializzato nelle chiamate successive (lazy init).
     */
    void optionsMenu() override;

    /// @return true se il tema Tessere è abilitato nella configurazione di Bruce.
    bool hasTheme() override { return bruceConfig.theme.tessere; }

    /// @return Il percorso del file di tema Tessere dalla configurazione di Bruce.
    String themePath() override { return bruceConfig.theme.paths.tessere; }
};

#endif // __TESSERE_H__
