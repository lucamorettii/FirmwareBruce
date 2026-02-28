/*#ifndef __MIKAI_H__
#define __MIKAI_H__

#include <MenuItemInterface.h>
#include <vector>

class Mikai : public MenuItemInterface {
public:
    Mikai() : MenuItemInterface("Mikai") {}
    void drawIcon(float scale);
    void optionsMenu();
    bool hasTheme() { return bruceConfig.theme.mikai; }
    String themePath() { return bruceConfig.theme.paths.mikai; }
};

#endif*/

#pragma once
/*
 * Mikai.h  â€“  Bruce firmware menu class
 */

#include <MenuItemInterface.h>
#include <vector>
#include "MikaiLogic.h"
#include "pn532_srix.h"

class Mikai : public MenuItemInterface {
public:
    Mikai() : MenuItemInterface("Mikai") {}
    void drawIcon(float scale) override;
    void optionsMenu() override;
    bool hasTheme() override { return bruceConfig.theme.mikai; }
    String themePath() override { return bruceConfig.theme.paths.mikai; }
};


