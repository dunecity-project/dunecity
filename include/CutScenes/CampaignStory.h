// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef CAMPAIGNSTORY_H
#define CAMPAIGNSTORY_H

#include <DataTypes.h>
#include <FileClasses/Palette.h>

#include <array>
#include <memory>
#include <string>

class Wsafile;

/// A family's campaign template and opponent roles, without changing runtime house identities.
struct CampaignStory {
    explicit CampaignStory(int house);
    int model;
    std::array<int, NUM_HOUSES> roles;
    bool enabled;

    std::string text(const std::string& original) const;
    void apply(Wsafile* animation) const;
private:
    std::shared_ptr<const Palette> paletteOverride;
    std::array<std::string, 4> roleNames;
};

#endif // CAMPAIGNSTORY_H
