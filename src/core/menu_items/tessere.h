/**
 * @file Tessere.h
 * @brief Dichiarazione della classe Tessere, voce di menu Bruce per tag MIFARE.
 *
 * Tessere implementa l'interfaccia MenuItemInterface di Bruce, aggiungendo
 * al firmware la gestione dei tag NFC MIFARE (Classic 1K/4K/Mini, Ultralight).
 * Espone:
 *   - Un'icona vettoriale scalabile che rappresenta una carta con chip EMV.
 *   - Il menu principale con Info, Read, Write e Auto Write.
 *   - Il supporto al sistema di theming di Bruce.
 */

#ifndef __TESSERE_H__
#define __TESSERE_H__

#include <MenuItemInterface.h>
#include <vector>

/**
 * @brief Voce di menu Bruce per la gestione dei tag NFC MIFARE.
 *
 * Eredita da MenuItemInterface e sovrascrive i tre metodi richiesti:
 *   - drawIcon()    → disegna l'icona nel launcher.
 *   - optionsMenu() → mostra il menu delle operazioni disponibili.
 *   - hasTheme() / themePath() → integrazione con il sistema di theming.
 */
class Tessere : public MenuItemInterface {
public:
    /// Costruisce la voce con il nome "Tessere" visibile nel launcher di Bruce.
    Tessere() : MenuItemInterface("Tessere") {}

    /**
     * @brief Disegna l'icona della carta con chip EMV nel launcher di Bruce.
     * @param scale Fattore di scala fornito da Bruce in base alla risoluzione.
     */
    void drawIcon(float scale) override;

    /**
     * @brief Mostra il menu principale con le operazioni disponibili sul tag.
     *
     * Voci del menu:
     *   - Info       → identifica il tag (UID, SAK, ATQA, tipo)
     *   - Read       → legge il dump e lo salva su SD
     *   - Write      → scrive un dump scelto dalla SD
     *   - Auto Write → scrive automaticamente il dump in base all'UID
     */
    void optionsMenu() override;

    /// @return true se il tema Tessere è abilitato nella configurazione di Bruce.
    bool hasTheme() override { return bruceConfig.theme.tessere; }

    /// @return Il percorso del file di tema Tessere dalla configurazione di Bruce.
    String themePath() override { return bruceConfig.theme.paths.tessere; }
};

#endif // __TESSERE_H__
