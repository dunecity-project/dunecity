/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DUNE2RREADINESSMENU_H
#define DUNE2RREADINESSMENU_H

#include "MenuBase.h"

#include <GUI/Label.h>
#include <GUI/ProgressBar.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <mod/Dune2RAssetManager.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>

/**
    Brings the canonical built-in Dune2R artwork up to its published target before a
    game starts: checks the latest trusted catalog, checksum-verifies what is
    installed, downloads only the missing or changed packs, and verifies the final
    installed target again. Native work runs on one worker at a time and is polled from
    update(), so the menu keeps drawing and Cancel stays responsive. Cancelling or
    failing keeps each unfinished pack in place and does not report ready. Browser work
    runs cooperatively on its single thread, yielding between network steps.
*/
class Dune2RReadinessMenu final : public MenuBase {
public:
    /// \param  manager               asset manager for the working Dune2R mod directory
    /// \param  checkPublishedCatalog look for a newer published catalog before verifying
    Dune2RReadinessMenu(std::unique_ptr<Dune2RAssetManager> manager, bool checkPublishedCatalog,
                           bool runCooperatively = false);
    ~Dune2RReadinessMenu() override;

    Dune2RReadinessMenu(const Dune2RReadinessMenu&) = delete;
    Dune2RReadinessMenu& operator=(const Dune2RReadinessMenu&) = delete;

    void update() override;

    /// True only when a complete installed target was verified after all work finished.
    bool isReady() const { return ready; }
    /// True when the menu has closed itself; no worker is running any more.
    bool isFinished() const { return finished; }
    /// True when nothing is running: finished, or stopped on a failure offering Retry.
    bool isSettled() const { return finished || phase == Phase::Failed; }
    bool wasCancelled() const { return cancelled; }
    const Dune2RAssetReadiness& getReadiness() const { return readiness; }
    const std::string& getMessage() const { return message; }
    /// Stop as soon as the running step can be abandoned, keeping installed packs.
    void requestCancel();

private:
    enum class Phase { CheckCatalog, Verify, Download, Confirm, Failed };

    void startCatalogCheck();
    void startVerify(Phase verifyPhase);
    void startDownload();
    void onRetry();
    void onCancel();
    void fail(const std::string& reason);
    void finish(bool success);
    void setWorking(bool working);
    void showDownloadProgress();
    void pumpProgress();
    std::launch launchPolicy() const;

    StaticContainer windowWidget;
    Label titleLabel;
    Label introLabel;
    Label statusLabel;
    TextProgressBar progressBar;
    TextButton retryButton;
    TextButton cancelButton;

    std::unique_ptr<Dune2RAssetManager> assetManager;
    std::future<Dune2RAssetInstallResult> installTask;
    std::future<Dune2RAssetReadiness> verifyTask;
    Dune2RAssetReadiness readiness;
    Phase phase = Phase::Verify;
    std::atomic<bool> cancelRequested{false};
    std::atomic<uint64_t> completedBytes{0};
    std::atomic<uint64_t> totalBytes{0};
    std::mutex progressTextMutex;
    std::string progressPack;
    std::string progressFile;
    std::string message;
    bool cooperative = false;
    bool catalogCheckFailed = false;
    bool ready = false;
    bool cancelled = false;
    bool finished = false;
};

/// Outcome of the automatic readiness gate. A message is set only when the caller
/// has to report it; the readiness menu explains and retries its own failures.
struct Dune2RArtworkGate {
    bool ready = false;
    std::string message;
};

/**
    Make the canonical built-in Dune2R artwork ready for a game that is about to
    start. Any other mod - vanilla, Tornie, DuneCity, a derived draft or an immutable
    shared snapshot - is reported ready without touching anything. A target that is
    already complete is recognised from a cheap size/modification-time digest, so a
    repeated menu interaction within 60 seconds costs no catalog request and no
    re-download. Later attempts refresh the catalog and checksum-verify again.
*/
Dune2RArtworkGate ensureDune2RArtworkReady(const std::string& modName);

/// Drop the cached readiness and its short-lived catalog check.
void forgetDune2RArtworkReadiness();

#endif // DUNE2RREADINESSMENU_H
