/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <Menu/Dune2RReadinessMenu.h>

#include <Colors.h>
#include <CursorManager.h>
#include <misc/FrameYield.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <globals.h>
#include <misc/DrawingRectHelper.h>
#include <mod/ModManager.h>
#include <sand.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <sstream>

namespace {

std::string formatSize(uint64_t bytes) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(bytes >= 10u * 1024u * 1024u ? 0 : 1)
         << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB";
    return text.str();
}

// The canonical built-in mod is the only artwork target this gate owns. Derived
// drafts keep their own content, and an immutable shared snapshot is pinned - its
// exact revision is supplied by shared-revision retrieval, not by this download.
constexpr const char* kCanonicalDune2RModName = "Dune2R";

struct ReadinessCache {
    std::string modPath;
    std::string fingerprint;
    bool ready = false;
    std::chrono::steady_clock::time_point checkedAt{};
};

constexpr auto kReadinessLifetime = std::chrono::seconds(60);

ReadinessCache& readinessCache() {
    static ReadinessCache cache;
    return cache;
}


} // namespace

Dune2RReadinessMenu::Dune2RReadinessMenu(std::unique_ptr<Dune2RAssetManager> manager,
                                         bool checkPublishedCatalog, bool runCooperatively)
    : assetManager(std::move(manager)), cooperative(runCooperatively) {
#ifdef __EMSCRIPTEN__
    // The shipped browser runtime has Asyncify, but no pthreads. Network calls
    // yield to JavaScript; deferred work must stay on this one SDL thread.
    cooperative = true;
#endif
    SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);

    const int panelWidth = std::min(580, getRendererWidth() - 20);
    const int panelHeight = std::min(260, getRendererHeight() - 20);
    const int originX = (getRendererWidth() - panelWidth) / 2;
    const int originY = (getRendererHeight() - panelHeight) / 2;

    titleLabel.setText(_("PREPARING DUNE2R ARTWORK"));
    titleLabel.setTextFontSize(22);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(originX + 10, originY + 12),
                           Point(panelWidth - 20, 30));

    introLabel.setText(_("The remastered artwork is checked and completed before the game starts."));
    introLabel.setAlignment(Alignment_HCenter);
    introLabel.setTextFontSize(14);
    windowWidget.addWidget(&introLabel, Point(originX + 20, originY + 48),
                           Point(panelWidth - 40, 24));

    statusLabel.setTextFontSize(14);
    statusLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_Top));
    windowWidget.addWidget(&statusLabel, Point(originX + 34, originY + 82),
                           Point(panelWidth - 68, 72));

    progressBar.setProgress(0.0);
    progressBar.setText(_("Checking"));
    progressBar.setColor(COLOR_GREEN);
    windowWidget.addWidget(&progressBar, Point(originX + 34, originY + 158),
                           Point(panelWidth - 68, 26));

    retryButton.setText(_("RETRY"));
    retryButton.setOnClick(std::bind(&Dune2RReadinessMenu::onRetry, this));
    cancelButton.setText(_("CANCEL"));
    cancelButton.setOnClick(std::bind(&Dune2RReadinessMenu::onCancel, this));
    const int buttonWidth = (panelWidth - 78) / 2;
    windowWidget.addWidget(&retryButton, Point(originX + 34, originY + panelHeight - 44),
                           Point(buttonWidth, 30));
    windowWidget.addWidget(&cancelButton, Point(originX + 44 + buttonWidth, originY + panelHeight - 44),
                           Point(buttonWidth, 30));
    cancelButton.setActive();

    if(checkPublishedCatalog && !assetManager->isReadOnly()) {
        startCatalogCheck();
    } else {
        startVerify(Phase::Verify);
    }
}

Dune2RReadinessMenu::~Dune2RReadinessMenu() {
    // Never outlive a worker that still holds this object's progress callback,
    // and never leave it running detached: ask it to stop, then join it.
    cancelRequested = true;
    if(installTask.valid() && installTask.wait_for(std::chrono::seconds(0)) != std::future_status::deferred) {
        installTask.wait();
    }
    if(verifyTask.valid() && verifyTask.wait_for(std::chrono::seconds(0)) != std::future_status::deferred) {
        verifyTask.wait();
    }
}

void Dune2RReadinessMenu::setWorking(bool working) {
    retryButton.setEnabled(!working);
    retryButton.setVisible(!working);
    // Quitting with Escape would destroy this menu while a worker runs; Cancel
    // stops the worker first and then closes.
    disableQuiting(working);
}

void Dune2RReadinessMenu::startCatalogCheck() {
    phase = Phase::CheckCatalog;
    setWorking(true);
    statusLabel.setText(_("Checking the published artwork catalog..."));
    progressBar.setProgress(0.0);
    progressBar.setText(_("Checking catalog"));
    installTask = std::async(launchPolicy(),
                             [this] { return assetManager->refreshCatalog(); });
}

void Dune2RReadinessMenu::startVerify(Phase verifyPhase) {
    phase = verifyPhase;
    setWorking(true);
    statusLabel.setText(verifyPhase == Phase::Confirm
        ? _("Verifying the installed artwork...")
        : _("Checking the installed artwork against the published checksums..."));
    completedBytes = 0;
    totalBytes = 0;
    {
        // The download's last pack and file must not be left standing over the
        // verification that follows it.
        std::lock_guard<std::mutex> lock(progressTextMutex);
        progressPack.clear();
        progressFile.clear();
    }
    progressBar.setProgress(0.0);
    progressBar.setText(_("Verifying"));
    verifyTask = std::async(launchPolicy(), [this] {
        return assetManager->checkReadiness([this](uint64_t checked, uint64_t total) {
            completedBytes = checked;
            totalBytes = total;
            if(cooperative) {
                // Checksumming the whole published target takes seconds on one
                // thread. Draw and take input between files, exactly as the
                // download does, so the browser tab never sits blocked and Cancel
                // stays usable. A native worker only reads the flag below.
                showByteProgress();
                pumpProgress();
            }
            return !cancelRequested.load();
        });
    });
}

void Dune2RReadinessMenu::startDownload() {
    phase = Phase::Download;
    setWorking(true);
    completedBytes = 0;
    totalBytes = readiness.missingBytes;
    {
        std::lock_guard<std::mutex> lock(progressTextMutex);
        progressPack.clear();
        progressFile.clear();
    }
    statusLabel.setText(std::to_string(readiness.missingPackIDs.size()) + _(" artwork pack(s) to complete, ")
                        + formatSize(readiness.missingBytes) + "\n"
                        + _("Downloading only the missing or changed files."));
    progressBar.setProgress(0.0);
    progressBar.setText(_("Starting download"));

    const auto packIDs = readiness.missingPackIDs;
    installTask = std::async(launchPolicy(), [this, packIDs] {
        return assetManager->install(packIDs, [this](const Dune2RAssetProgress& progress) {
            completedBytes = progress.completedBytes;
            totalBytes = progress.totalBytes;
            {
                std::lock_guard<std::mutex> lock(progressTextMutex);
                progressPack = progress.packName;
                progressFile = progress.filename;
            }
            if(cooperative) {
                showByteProgress();
                pumpProgress();
            }
            // Returning false keeps this unfinished staged pack from replacing
            // its installed counterpart. Completed packs remain verified.
            return !cancelRequested.load();
        });
    });
}

std::launch Dune2RReadinessMenu::launchPolicy() const {
    return cooperative ? std::launch::deferred : std::launch::async;
}

void Dune2RReadinessMenu::pumpProgress() {
    // No recursive update(): the current deferred future is executing here.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    draw();
    presentWithCursor();
    yieldFrameToBrowser(1);
    SDL_Event event{};
    while(SDL_PollEvent(&event)) {
        if(event.type == SDL_QUIT
           || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
            requestCancel();
        } else {
            doInput(event);
        }
    }
}

void Dune2RReadinessMenu::showByteProgress() {
    const uint64_t completed = completedBytes.load();
    const uint64_t total = totalBytes.load();
    progressBar.setProgress(total == 0 ? 0.0 : 100.0 * static_cast<double>(completed)
                                                   / static_cast<double>(total));
    progressBar.setText(formatSize(completed) + " / " + formatSize(total));
    std::lock_guard<std::mutex> lock(progressTextMutex);
    if(!progressPack.empty()) {
        statusLabel.setText(progressPack + "\n" + progressFile);
    }
}

void Dune2RReadinessMenu::fail(const std::string& reason) {
    phase = Phase::Failed;
    setWorking(false);
    message = reason;
    statusLabel.setText(std::string(_("Artwork is not ready: ")) + reason + "\n"
                        + _("Retry to continue the download, or cancel to choose another mod."));
    progressBar.setText(_("Not ready"));
    retryButton.setActive();
}

void Dune2RReadinessMenu::finish(bool success) {
    ready = success;
    cancelled = !success && cancelRequested.load();
    finished = true;
    setWorking(false);
    disableQuiting(false);
    quit();
}

void Dune2RReadinessMenu::onRetry() {
    if(phase != Phase::Failed) {
        return;
    }
    message.clear();
    // A failed catalog check is retried too; a missing update must not be mistaken
    // for a complete target, and the retry may be the first moment with a network.
    if(catalogCheckFailed && !assetManager->isReadOnly()) {
        catalogCheckFailed = false;
        startCatalogCheck();
    } else {
        startVerify(Phase::Verify);
    }
}

void Dune2RReadinessMenu::onCancel() {
    requestCancel();
}

void Dune2RReadinessMenu::requestCancel() {
    if(finished) {
        return;
    }
    cancelRequested = true;
    if(phase == Phase::Failed) {
        finish(false);
        return;
    }
    statusLabel.setText(_("Cancelling; verified downloads are kept for retry..."));
    progressBar.setText(_("Cancelling"));
}

void Dune2RReadinessMenu::update() {
    if(finished) {
        return;
    }

    switch(phase) {
        case Phase::CheckCatalog: {
            if(installTask.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
                return;
            }
            if(cooperative) {
                pumpProgress();
                if(cancelRequested.load()) { finish(false); return; }
            }
            const auto result = installTask.get();
            catalogCheckFailed = !result.success;
            SDL_Log("Dune2R readiness: catalog check %s (%s)",
                    result.success ? "updated" : "kept the installed catalog",
                    result.message.c_str());
            if(cancelRequested.load()) {
                finish(false);
                return;
            }
            startVerify(Phase::Verify);
            return;
        }

        case Phase::Verify:
        case Phase::Confirm: {
            // A native worker reports its checked bytes through the same counters.
            showByteProgress();
            if(verifyTask.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
                return;
            }
            const bool confirming = phase == Phase::Confirm;
            if(cooperative) {
                pumpProgress();
                if(cancelRequested.load()) { finish(false); return; }
            }
            readiness = verifyTask.get();
            if(cancelRequested.load()) {
                finish(false);
                return;
            }
            if(readiness.complete) {
                if(pGFXManager != nullptr) {
                    // The frame loader caches mounted packs; a completed install has
                    // to be visible to the very next game. The install itself also
                    // stamps the mount revision, so a refresh problem here must not
                    // turn a verified target into a blocked start.
                    try {
                        pGFXManager->reloadEnhancedUnitMounts();
                    } catch(const std::exception& error) {
                        SDL_Log("Dune2R readiness: mount refresh deferred (%s)", error.what());
                    }
                }
                progressBar.setProgress(100.0);
                progressBar.setText(_("Verified"));
                statusLabel.setText(_("Artwork verified: ") + formatSize(readiness.totalBytes));
                finish(true);
                return;
            }
            if(cancelRequested.load()) {
                finish(false);
                return;
            }
            if(confirming) {
                fail(_("the downloaded artwork did not verify completely"));
                return;
            }
            if(readiness.missingPackIDs.empty()) {
                fail(_("the artwork catalog describes no packs"));
                return;
            }
            if(assetManager->isReadOnly()) {
                fail(_("this is a shared, immutable version; its artwork cannot be downloaded here"));
                return;
            }
            startDownload();
            return;
        }

        case Phase::Download: {
            showByteProgress();
            if(installTask.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
                return;
            }
            if(cooperative) {
                pumpProgress();
                if(cancelRequested.load()) { finish(false); return; }
            }
            const auto result = installTask.get();
            if(!result.success) {
                if(cancelRequested.load()) {
                    finish(false);
                } else {
                    fail(result.message);
                }
                return;
            }
            // Never start on staged bytes: the installed target is checked again.
            startVerify(Phase::Confirm);
            return;
        }

        case Phase::Failed:
            return;
    }
}

Dune2RArtworkGate ensureDune2RArtworkReady(const std::string& modName) {
    if(modName != kCanonicalDune2RModName) {
        // Shared snapshot names and derived drafts keep their own content untouched.
        return {true, {}};
    }
    auto& mods = ModManager::instance();
    if(!mods.isInitialized() || !mods.modExists(modName)) {
        return {false, _("The Dune2R mod is unavailable. Repair the installation, then try again.")};
    }

    const auto modPath = mods.getModPath(modName);
    std::unique_ptr<Dune2RAssetManager> manager;
    try {
        manager = std::make_unique<Dune2RAssetManager>(modPath);
    } catch(const std::exception& error) {
        // Without a catalog nothing can be verified, so missing artwork must not be
        // treated as ready. The chooser stays open and Next can be pressed again.
        readinessCache() = {};
        return {false, std::string(_("The Dune2R artwork catalog could not be read: "))
                       + error.what() + _("\nRepair the installation, then try again.")};
    }
    if(manager->isReadOnly()) {
        // An immutable shared version is pinned content; writing into it is refused
        // by the asset manager, and its exact revision arrives through the shared
        // revision it was installed from.
        return {true, {}};
    }

    const auto fingerprint = manager->installedFingerprint();
    const auto& cache = readinessCache();
    if(cache.ready && cache.modPath == modPath && cache.fingerprint == fingerprint
       && std::chrono::steady_clock::now() - cache.checkedAt < kReadinessLifetime) {
        return {true, {}};
    }

    Dune2RReadinessMenu menu(std::move(manager), true);
    menu.showMenu();
    if(!menu.isReady()) {
        readinessCache() = {};
        return {false, {}};
    }
    readinessCache() = {modPath, menu.getReadiness().fingerprint, true,
                        std::chrono::steady_clock::now()};
    return {true, {}};
}

void forgetDune2RArtworkReadiness() {
    readinessCache() = {};

}
