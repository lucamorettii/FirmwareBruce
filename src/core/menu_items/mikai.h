/**
 * @file Mikai.h
 * @brief Dichiarazione della classe Mikai, voce di menu Bruce per i tag SRIX4K.
 *
 * Mikai implementa l'interfaccia MenuItemInterface di Bruce, aggiungendo
 * al firmware la gestione dei tag NFC SRIX4K (MyKey). Espone:
 *   - Un'icona vettoriale scalabile che rappresenta il lettore NFC.
 *   - Il menu principale con le operazioni disponibili sul tag.
 *   - Il supporto al sistema di theming di Bruce.
 */

#ifndef __MIKAI_H__
#define __MIKAI_H__

#include "itsFree/mikai/MikaiLogic.h"
#include "pn532_srix.h"
#include <MenuItemInterface.h>
#include <vector>

/**
 * @brief Voce di menu Bruce per la gestione dei tag NFC SRIX4K (MyKey / Mikai).
 *
 * Eredita da MenuItemInterface e sovrascrive i tre metodi richiesti da Bruce:
 *   - drawIcon()    → disegna l'icona nel launcher.
 *   - optionsMenu() → inizializza l'NFC e mostra il menu delle operazioni.
 *   - hasTheme() / themePath() → integrazione con il sistema di theming.
 */
class Mikai : public MenuItemInterface {
public:
    /// Costruisce la voce con il nome "Mikai" visibile nel launcher di Bruce.
    Mikai() : MenuItemInterface("Mikai") {}

    /**
     * @brief Disegna l'icona del lettore NFC nel launcher di Bruce.
     * @param scale Fattore di scala fornito da Bruce in base alla dimensione dello schermo.
     */
    void drawIcon(float scale) override;

    /**
     * @brief Inizializza il driver PN532 (una sola volta) e mostra il menu principale.
     *
     * Se il PN532 non risponde all'inizializzazione, mostra un errore e ritorna.
     * Altrimenti presenta il sottomenu con Info, Add credit, Set credit, Reset,
     * Export/Import vendor.
     */
    void optionsMenu() override;

    /// @return true se il tema Mikai è abilitato nella configurazione di Bruce.
    bool hasTheme() override { return bruceConfig.theme.mikai; }

    /// @return Il percorso del file di tema Mikai dalla configurazione di Bruce.
    String themePath() override { return bruceConfig.theme.paths.mikai; }
};

#endif // __MIKAI_H__
