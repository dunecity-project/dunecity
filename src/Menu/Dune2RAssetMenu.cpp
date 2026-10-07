/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <Menu/Dune2RAssetMenu.h>

#include <Colors.h>
#include <CursorManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <globals.h>
#include <misc/FrameYield.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <sand.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>

namespace {

std::launch assetTaskPolicy() {
#ifdef __EMSCRIPTEN__
    return std::launch::deferred;
#else
    return std::launch::async;
#endif
}

std::string formatSize(uint64_t bytes) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(bytes >= 10u * 1024u * 1024u ? 0 : 1)
         << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB";
    return text.str();
}

} // namespace

Dune2RAssetMenu::Dune2RAssetMenu() {
    SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);

    const int panelWidth = std::min(580, getRendererWidth() - 20);
    const int panelHeight = std::min(340, getRendererHeight() - 20);
    const int originX = (getRendererWidth() - panelWidth) / 2;
    const int originY = (getRendererHeight() - panelHeight) / 2;

    titleLabel.setText(_("DUNE2R ASSET PACKS"));
    titleLabel.setTextFontSize(22);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(originX + 10, originY + 12),
                           Point(panelWidth - 20, 30));

    introLabel.setText(_("Download verified optional art into the selected working mod."));
    introLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&introLabel, Point(originX + 20, originY + 48),
                           Point(panelWidth - 40, 24));

    packLabel.setText(_("Asset pack"));
    windowWidget.addWidget(&packLabel, Point(originX + 34, originY + 91), Point(105, 26));
    packDropDown.setOnSelectionChange(
        std::bind(&Dune2RAssetMenu::onSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&packDropDown, Point(originX + 142, originY + 88),
                           Point(panelWidth - 176, 28));

    statusLabel.setTextFontSize(14);
    statusLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_Top));
    windowWidget.addWidget(&statusLabel, Point(originX + 34, originY + 132),
                           Point(panelWidth - 68, 72));

    progressBar.setProgress(0.0);
    progressBar.setText(_("Ready"));
    progressBar.setColor(COLOR_GREEN);
    windowWidget.addWidget(&progressBar, Point(originX + 34, originY + 213),
                           Point(panelWidth - 68, 26));

    downloadButton.setText(_("DOWNLOAD"));
    downloadButton.setOnClick(std::bind(&Dune2RAssetMenu::onDownload, this));
    refreshButton.setText(_("REFRESH"));
    refreshButton.setOnClick(std::bind(&Dune2RAssetMenu::onRefreshCatalog, this));
    backButton.setText(_("BACK"));
    backButton.setOnClick(std::bind(&Dune2RAssetMenu::onBack, this));
    const int buttonWidth = (panelWidth - 88) / 3;
    windowWidget.addWidget(&refreshButton, Point(originX + 34, originY + panelHeight - 48), Point(buttonWidth, 30));
    windowWidget.addWidget(&downloadButton, Point(originX + 44 + buttonWidth, originY + panelHeight - 48), Point(buttonWidth, 30));
    windowWidget.addWidget(&backButton, Point(originX + 54 + 2 * buttonWidth, originY + panelHeight - 48), Point(buttonWidth, 30));

    try {
        assetManager = std::make_unique<Dune2RAssetManager>(
            ModManager::instance().getModPath(ModManager::instance().getActiveModName()));
        populatePacks();
    } catch(const std::exception& error) {
        statusLabel.setText(std::string(_("Asset catalog error: ")) + error.what());
    }

    const bool ready = assetManager != nullptr && !assetManager->getPacks().empty();
    const bool writable = assetManager != nullptr && !assetManager->isReadOnly();
    packDropDown.setEnabled(ready);
    downloadButton.setEnabled(ready && writable);
    refreshButton.setEnabled(writable);
    if(assetManager != nullptr && assetManager->isReadOnly()) {
        introLabel.setText(_("Online snapshot: installed packs can be inspected."));
    }
    if(ready) {
        refreshSelectionStatus();
    }
}

void Dune2RAssetMenu::populatePacks() {
    packDropDown.clearAllEntries();
    const auto& packs = assetManager->getPacks();
    if(packs.empty()) return;
    packDropDown.addEntry(_("Remastered Assets: ALL"), 0);
    for(size_t i = 0; i < packs.size(); ++i) {
        packDropDown.addEntry(packs[i].displayName, static_cast<int>(i + 1));
    }
    packDropDown.setSelectedItem(0);
}

void Dune2RAssetMenu::onRefreshCatalog() {
    if(assetManager == nullptr || assetManager->isReadOnly() || downloading) return;
    downloading = true;
    refreshingCatalog = true;
    packDropDown.setEnabled(false);
    downloadButton.setEnabled(false);
    refreshButton.setEnabled(false);
    backButton.setEnabled(false);
    disableQuiting(true);
    statusLabel.setText(_("Checking the published asset catalog..."));
    progressBar.setText(_("Checking"));
    installTask = std::async(assetTaskPolicy(), [this] { return assetManager->refreshCatalog(); });
}

Dune2RAssetMenu::~Dune2RAssetMenu() {
    if(installTask.valid()) {
        installTask.wait();
    }
}

std::vector<std::string> Dune2RAssetMenu::selectedPackIDs() const {
    std::vector<std::string> result;
    if(assetManager == nullptr) {
        return result;
    }
    const auto& packs = assetManager->getPacks();
    const int selected = packDropDown.getSelectedEntryIntData();
    if(selected == 0) {
        for(const auto& pack : packs) {
            result.push_back(pack.id);
        }
    } else if(selected > 0 && selected <= static_cast<int>(packs.size())) {
        result.push_back(packs[static_cast<size_t>(selected - 1)].id);
    }
    return result;
}

void Dune2RAssetMenu::onSelectionChanged(bool) {
    refreshSelectionStatus();
}

void Dune2RAssetMenu::refreshSelectionStatus() {
    if(assetManager == nullptr || downloading) {
        return;
    }
    const auto ids = selectedPackIDs();
    uint64_t bytes = 0;
    int installed = 0;
    for(const auto& pack : assetManager->getPacks()) {
        if(std::find(ids.begin(), ids.end(), pack.id) == ids.end()) {
            continue;
        }
        bytes += pack.totalBytes();
        if(assetManager->isPackInstalled(pack)) {
            ++installed;
        }
    }
    statusLabel.setText(
        std::to_string(ids.size()) + _(" pack(s), ") + formatSize(bytes)
        + "\n" + std::to_string(installed) + _(" installed and checksum-verified.")
        + "\n" + (assetManager->isReadOnly()
            ? _("Switch to the Dune2R working mod to download assets.")
            : _("Retry keeps verified files and continues the download.")));
    progressBar.setProgress(ids.empty() ? 0.0 : 100.0 * installed / ids.size());
    progressBar.setText(installed == static_cast<int>(ids.size()) ? _("Verified") : _("Ready"));
}

void Dune2RAssetMenu::onDownload() {
    if(assetManager == nullptr || assetManager->isReadOnly() || downloading) {
        return;
    }
    const auto ids = selectedPackIDs();
    if(ids.empty()) {
        return;
    }

    downloading = true;
    completedBytes = 0;
    totalBytes = 0;
    packDropDown.setEnabled(false);
    downloadButton.setEnabled(false);
    refreshButton.setEnabled(false);
    backButton.setEnabled(false);
    disableQuiting(true);
    statusLabel.setText(_("Connecting to the Dune2R asset repository..."));
    progressBar.setProgress(0.0);
    progressBar.setText(_("Starting download"));

    installTask = std::async(assetTaskPolicy(), [this, ids] {
        return assetManager->install(ids, [this](const Dune2RAssetProgress& progress) {
            completedBytes = progress.completedBytes;
            totalBytes = progress.totalBytes;
            {
                std::lock_guard<std::mutex> lock(progressTextMutex);
                progressPack = progress.packName;
                progressFile = progress.filename;
            }
#ifdef __EMSCRIPTEN__
            // This callback runs between network steps on the single SDL thread.
            // Keep drawing/input live without recursively polling installTask.
            statusLabel.setText(progress.packName + "\n" + progress.filename);
            progressBar.setProgress(progress.totalBytes == 0 ? 0.0
                : 100.0 * progress.completedBytes / progress.totalBytes);
            progressBar.setText(formatSize(progress.completedBytes) + " / "
                                + formatSize(progress.totalBytes));
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            draw();
            presentWithCursor();
            yieldFrameToBrowser(1);
            SDL_Event event{};
            while(SDL_PollEvent(&event)) doInput(event);
#endif
            return true;
        });
    });
}

void Dune2RAssetMenu::update() {
    if(!downloading) {
        return;
    }
    if(refreshingCatalog) {
        if(installTask.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) return;
        const auto result = installTask.get();
        refreshingCatalog = false;
        downloading = false;
        if(result.success) populatePacks();
        const bool ready = !assetManager->getPacks().empty();
        packDropDown.setEnabled(ready);
        downloadButton.setEnabled(ready && !assetManager->isReadOnly());
        refreshButton.setEnabled(!assetManager->isReadOnly());
        backButton.setEnabled(true);
        disableQuiting(false);
        statusLabel.setText(result.message);
        progressBar.setText(result.success ? _("Catalog ready") : _("Offline catalog"));
        return;
    }
    const uint64_t completed = completedBytes.load();
    const uint64_t total = totalBytes.load();
    progressBar.setProgress(total == 0 ? 0.0 : 100.0 * completed / total);
    progressBar.setText(formatSize(completed) + " / " + formatSize(total));
    {
        std::lock_guard<std::mutex> lock(progressTextMutex);
        if(!progressPack.empty()) {
            statusLabel.setText(progressPack + "\n" + progressFile);
        }
    }

    if(installTask.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
        return;
    }

    const Dune2RAssetInstallResult result = installTask.get();
    downloading = false;
    const bool ready = !assetManager->getPacks().empty();
    packDropDown.setEnabled(ready);
    downloadButton.setEnabled(ready && !assetManager->isReadOnly());
    refreshButton.setEnabled(!assetManager->isReadOnly());
    backButton.setEnabled(true);
    disableQuiting(false);
    statusLabel.setText((result.success ? std::string(_("OK: ")) : std::string(_("ERROR: ")))
                        + result.message);
    if(result.success) {
        progressBar.setProgress(100.0);
        progressBar.setText(_("Installed and verified"));
        pGFXManager->reloadEnhancedUnitMounts();
        try {
            const auto revision = Workshop::saveMod(ModManager::instance().getActiveModName());
            statusLabel.setText(_("Assets installed. Saved mod version ") + std::to_string(revision.version));
        } catch(const std::exception& error) {
            statusLabel.setText(std::string(_("Assets installed, but version save failed: ")) + error.what());
        }
    } else {
        progressBar.setText(_("Download paused; retry to resume"));
    }
}

void Dune2RAssetMenu::onBack() {
    if(!downloading) {
        quit();
    }
}
