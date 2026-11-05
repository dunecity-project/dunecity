/*
 *  This file is part of Dune City.
 *
 *  Licensed under GPL-2.0-or-later.
 */

#include <Menu/HowToPlayMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

// Inline copy of HOWTOPLAY.md condensed for the in-game viewer. Kept in
// sync by hand — the full long-form doc lives at the repo root and on
// dunelegacy.com/dune-city.html. Plain text only (TextView can't render
// markdown), so headings are upper-cased and bullets use ASCII dashes.
static const char* kHowToPlayBody =
    "DUNE CITY layers a Micropolis-style city simulation on top of\n"
    "Dune II. Build a Construction Yard, lay out roads, drop R/C/I\n"
    "zone tiles, and keep the lights on. The simulation does the rest.\n"
    "\n"
    "1) TURN ON CITY-SIM MODE\n"
    "On first launch Dune City offers an 'Enable now' prompt for the\n"
    "bundled 'dunecity' mod. If you skipped it, open Mods from the main\n"
    "menu, pick 'Dune City', and click Activate. The active mod is\n"
    "shown in the bottom-left watermark.\n"
    "\n"
    "2) START A CITY-SIZED SKIRMISH\n"
    "Single Player -> Custom Game -> pick a large map. The bundled map\n"
    "'2P - 192x192 - SimCity' is designed for city-sim play. City tools\n"
    "only work on rock tiles; sand is for spice and combat as usual.\n"
    "\n"
    "3) LAY DOWN THE BACKBONE\n"
    "- Construction Yard: your anchor. Place on rock first.\n"
    "- Wind Traps / Nuclear Plants: power everything. Without surplus\n"
    "  power, zones decay.\n"
    "- Refinery + Silos: spice income funds the build queue.\n"
    "- Roads: a 'Road' tile appears in the build menu when city-sim\n"
    "  mode is on. Roads carry supply between Industrial, Commercial,\n"
    "  and Residential. Without roads, zones stall at Level 1.\n"
    "Roads cost 10 credits and build instantly. They are not units.\n"
    "\n"
    "4) ZONE THE CITY\n"
    "Three new buildable tiles appear when city-sim is active:\n"
    "- Residential (R): population. Needs jobs and low pollution.\n"
    "- Commercial (C):  jobs and services. Needs people + supply.\n"
    "- Industrial (I):  production. Cluster away from R - pollution\n"
    "  drops land value.\n"
    "Each zone is a 2x2 tile. Zones start empty and develop over time\n"
    "based on land value, power, supply, and crime.\n"
    "\n"
    "5) MANAGE THE BUDGET\n"
    "Population pays taxes annually. Police stations cost upkeep.\n"
    "Open the City Budget window from the HUD to set tax rate and\n"
    "police funding %. Underfunded police -> rising crime -> falling\n"
    "land value -> stalled growth. Every player runs their own budget.\n"
    "\n"
    "6) KEEP GROWING\n"
    "- Spread police stations - they have a coverage radius.\n"
    "- Cluster industry at the city edge.\n"
    "- Watch the power meter; build nuclear before you run out.\n"
    "- Drop an inner Industrial spur near Commercial - C zones need\n"
    "  supply within ~16 tiles via road.\n"
    "\n"
    "COMBAT STILL APPLIES\n"
    "Dune City is layered on top of Dune Legacy. Units, vehicles,\n"
    "ornithopters, sandworms and combat all still work. Play a pure\n"
    "city-builder skirmish on a big rock map, or a hybrid where you\n"
    "defend the colony while it grows.\n"
    "QuantBot uses captured enemy harvesters for spice income. A\n"
    "borrowed worker returns to its nearest own refinery at half\n"
    "capacity, or with spice and ten seconds of deviation remaining.\n"
    "Human orders take priority; human harvester behaviour is unchanged.\n"
    "Income goes to the refinery unloading it, even after reversion.\n"
    "\n"
    "SAVE A FAVOURITE MAP\n"
    "Game menu -> Save as Map (Random Skirmish, Civilization Wars,\n"
    "or Custom Game, including spectating; not campaign missions).\n"
    "- From the start: the original embedded map, unchanged. Your\n"
    "  chosen name names the file; its internal map name stays intact.\n"
    "- From right now: current terrain/spice types, concrete/roads,\n"
    "  buildings and units with map-format health, houses, teams and\n"
    "  credits. Carried/contained/off-map units use nearby legal tiles.\n"
    "- Enter a name, Save, and confirm before replacing a file.\n"
    "  Find it in Custom Game -> MP User Maps (2+ players) or\n"
    "  SP User Maps (1 player), and the map editor's user maps.\n"
    "A current-state export is a new map: exact spice amounts/cargo,\n"
    "city residents/overlays, research, queues, targets, paths, fog,\n"
    "statistics and reinforcements reset. Map-supported zone density,\n"
    "wind-trap upgrades and facing/attack modes are kept. Native\n"
    "sandworms stay; native crime units are omitted. Normal fresh-map\n"
    "road rings/refinery harvesters still apply. For the complete\n"
    "ongoing match, use Save Game instead.\n"
    "\n"
    "MOUSE CAMERA CONTROLS (GAME AND MAP EDITOR)\n"
    "- Physical wheel up/down: zoom in/out at the pointer.\n"
    "- Mac trackpad: move two fingers to pan, spread/pinch to zoom.\n"
    "  Two-finger scrolling never zooms. No mouse button needed.\n"
    "- iPhone/iPad: move two fingers to pan, spread/pinch to zoom\n"
    "  around their midpoint. Start both fingers over the map.\n"
    "- Hold middle mouse (button 3) and drag: the map follows the\n"
    "  cursor. Drag left to move the view right. Release to stop.\n"
    "- Sidebars, lists and menus keep their wheel scrolling.\n"
    "- In the editor, Shift+wheel over a unit rotates it.\n"
    "\n"
    "MORE\n"
    "- Full guide: https://dunelegacy.com/dune-city.html\n"
    "- Source / issues: https://github.com/svan058/dunecity\n";

HowToPlayMenu::HowToPlayMenu() : MenuBase()
{
    SDL_Texture* pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    SDL_Texture* pPlanetBackground = pGFXManager->getUIGraphic(UI_PlanetBackground);
    planetPicture.setTexture(pPlanetBackground);
    SDL_Rect dest1 = calcAlignedDrawingRect(pPlanetBackground);
    dest1.y = dest1.y - getHeight(pPlanetBackground) / 2 + 10;
    windowWidget.addWidget(&planetPicture, dest1);

    SDL_Texture* pDuneLegacy = pGFXManager->getUIGraphic(UI_DuneLegacy);
    duneLegacy.setTexture(pDuneLegacy);
    SDL_Rect dest2 = calcAlignedDrawingRect(pDuneLegacy);
    dest2.y = dest2.y + getHeight(pDuneLegacy) / 2 + 28;
    windowWidget.addWidget(&duneLegacy, dest2);

    title.setText(_("HOW TO PLAY DUNE CITY"));
    title.setTextFontSize(20);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title,
                           Point(getRendererWidth() / 2 - 280,
                                 getRendererHeight() / 2 - 200),
                           Point(560, 30));

    body.setTextFontSize(14);
    body.setText(kHowToPlayBody);
    body.setAutohideScrollbar(false);
    windowWidget.addWidget(&body,
                           Point(getRendererWidth() / 2 - 320,
                                 getRendererHeight() / 2 - 160),
                           Point(640, 320));

    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&HowToPlayMenu::onBack, this));
    windowWidget.addWidget(&backButton,
                           Point(getRendererWidth() / 2 - 60,
                                 getRendererHeight() / 2 + 175),
                           Point(120, 30));
}

HowToPlayMenu::~HowToPlayMenu() = default;

void HowToPlayMenu::onBack() {
    quit();
}
