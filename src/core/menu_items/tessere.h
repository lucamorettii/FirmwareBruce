#ifndef __Tessere_H__
#define __Tessere_H__

#include <MenuItemInterface.h>
#include <vector>

class Tessere : public MenuItemInterface {
public:
    Tessere() : MenuItemInterface("Tessere") {}
    void drawIcon(float scale) override;
    void optionsMenu() override;
    bool hasTheme() { return bruceConfig.theme.tessere; }
    String themePath() { return bruceConfig.theme.paths.tessere; }
};

#endif
