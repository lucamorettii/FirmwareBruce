#ifndef __MIKAI_H__
#define __MIKAI_H__

#include "MikaiLogic.h"
#include "pn532_srix.h"
#include <MenuItemInterface.h>
#include <vector>

class Mikai : public MenuItemInterface {
public:
    Mikai() : MenuItemInterface("Mikai") {}
    void drawIcon(float scale) override;
    void optionsMenu() override;
    bool hasTheme() override { return bruceConfig.theme.mikai; }
    String themePath() override { return bruceConfig.theme.paths.mikai; }
};

#endif
