# Mac staging package pipeline — 2026-11-28

## Acceptance, recorded before implementation

Stefan: “why does it take so long to compile dune? something is not being done correctly”; “why does it take 30 minutes to get the latest build on my mba. it seems like there is just too much process bloat”.

| Requirement | Measurable acceptance / enforcement |
| --- | --- |
| Explicit on-demand staging is not silently debounced | `plan()` passes `manual=True` for workflow_dispatch with staging_sha, including integration_run_id; tests cover both forms, schedules, existing published/draft snapshots and failed qualification. |
| The pushed Mac build is not discarded | Integration pushes set Mac PACKAGE_BUILD, uploading the normal ZIP, DMG and source/build metadata as DuneCity-macOS-DMG directly from build-macos. No browser/Linux job dependency. |
| Retain honest qualification | Early artifact means Mac integration checks/package preparation passed, not all-platform CI green. Job summary says so. Existing complete CI and staging publication gates remain required. |
| Do not compile the same Mac candidate twice | Staging selection finds independently successful exact-SHA CI and its unexpired Mac artifact. build-macos downloads, checks source/dev identity/completeness and reuploads those bytes; dependency install/compile/test steps are skipped. Failed downloads or mismatches fail, not silently rebuild. |
| Keep scheduled/nightly/production guarantees | Automatic dispatch checks the two-hour debounce before dispatch; scheduled selection retains it. Daily Mac nightly still runs its GUI coverage. Stable Mini signing/admission/publication behavior is unchanged. |
| Browser compiler caching is real | Emscripten 4.0.14 supports EM_COMPILER_WRAPPER; CI sets it to ccache, restores objects under an SDK-version/revision key and prints fresh statistics. No unmeasured speedup claimed. |

Missing/expired artifacts from old CI runs legitimately take the original Mac packaging path. Nightly coverage legitimately rebuilds/runs its own tier. Other platforms' staging rebuilds remain unchanged: cross-platform producer/consumer redesign is outside this change.

## Measured baseline and critical path estimate

Evidence: prior session 5cdedcba-da99-4e42-9e2c-8c75afe13568 and downloaded logs, plus job timestamps retrieved from GitHub for CI 46090128420 and staging 46091445422 (same source 47dfee5c1).

- Push Mac job 00:51:54–00:57:27: 5m33s. Lane 4m10s is **not compile time**: actual compile phase was 23s; Mac integration tests took 164s.
- Staging Mac job 01:07:26–01:17:18: 9m52s; lane 8m03s; actual compile again 23s. Extra time is packaging/producer-bound pointer fixture preparation, not a slower compiler. Artifact upload completed 01:16:44.
- Push browser job 14m43s; browser build step 13m15s. Staging browser build step 9m01s. The script had no compiler cache wrapper and CI restored no browser compiler cache. The browser step includes SDK setup, SDL ports, compile, link and verification, not just compile.
- Linux tests ran in both workflows (push Run tests 7m46s; staging 5m38s).
- From initial Mac job start to downloadable staging Mac artifact: **24m50s**. To published immutable staging release at 01:19:48: **27m54s** (plus initial selection/runner wait).
- After this change, warm-cache Mac artifact estimate: **about 9–10 minutes after push**, using the measured existing full package lane and artifact upload, without waiting 15 minutes for other CI. Roughly 15 minutes removed from artifact availability; no hosted before/after run has occurred. Publication still waits for complete qualification and other packages.
- The earlier 6–8 minute aspiration is not established by these samples: preserving the full existing package/fixture preparation costs about 4 minutes beyond the 5m33s CI job. GitHub runner waits and cold-cache/source-change costs remain external/variable.

Measured cache hit rates from those two runs: Mac 731/732 (99.86%) and 732/735 (99.59%); Windows 537/537 (100%) in each; Linux 537/537 (100%) in each. Browser previously had no application compiler cache, so no hit-rate denominator exists (not a measured 0%). A later changed-source Mac run 46105564100 had 423/733 hits (57.71%) and a 507s compile phase: a warm cache cannot remove genuine changed translation-unit work. New browser cache first-use/hit rate and default-main/tag cache accessibility remain unverified until real CI; no speculative numerical browser speedup is included.

## Validation and activation

Validate the immutable off-branch test commit using actionlint 1.7.12 and `python3 -m unittest discover -s scripts/tests -p test_release_automation.py`, plus affected release-delivery/iOS routing tests, from a private git-archive snapshot. The coordinator's handoff carries the exact test commit, command outcomes and workflow SHA-256; no branch commit/push is made here.

Stefan must approve the **exact build.yml SHA-256** reported with that test commit for rebinding in protected `~/.config/agent-workspace/github-runner.json`. Do not edit that file without his approval; coordinate rebinding after affected live Mini jobs finish. Integrator then lands/pushes the tested hunks. Routine staging remains hosted ARM64/unsigned; production, signing/notarisation, Latest and stable browser paths are not bypassed. No production release/tag or Air install is requested or performed. First real early-artifact/reuse and browser-cache consumers remain operational proof, not inferred completion.
