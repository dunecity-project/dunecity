// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <globals.h>
#include <mod/ModManager.h>

namespace healing {
// Captured factory technology controls healing reload, not current ownership.
inline int reload(int base, int technologyHouse) {
    return ModManager::instance().isTornieFamilyContentActive()
        && isHouseFaction(static_cast<HOUSETYPE>(technologyHouse), HOUSE_THARPIQUE)
        ? base * 3 / 2 : base;
}
}
