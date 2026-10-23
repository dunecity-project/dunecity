# Shared agent staging and disposable probe assets

The local staging checkout on claw.local is `dunecity-feature-batch`, branch
`integrate/feature-batch`, with native Release build `build-int`. Codex chats and
subagents, OMP sessions/task/eval agents, and Claude Code sessions/subagents use
the same lane. A new request does not require a new CI environment or build copy.

On managed hosts, read `~/.config/agent-workspace/POLICY.md` and run
`agent-workspace status`. Contributors may edit disjoint claimed paths. Shared
files require handoff or a source-only worktree. One integrator stages explicit
paths and commits a batch; contributors do not switch/reset the common branch
or independently bump versions and release. All edit claims must be released
before the integration build, including the builder's own claims.

```sh
agent-workspace claim --project dunecity --owner omp:SESSION:CHILD src/OwnedFile.cpp
agent-workspace release --project dunecity --owner omp:SESSION:CHILD
agent-workspace stage --project dunecity --owner codex:SESSION:main --wait verify
```

The staging wrapper checks CMake's source path, requires committed clean source,
holds a shared process-independent mutex, checks Ninja dependency records,
builds incrementally, and runs CTest serially against the combined source.
Its receipt records the source commit and command result. Run it in a fresh
named Tern tab and verify completion. Probe processes compile current source
against existing game objects, so builds, source mutations and test runs must
not overlap. The old `build` directory in this worktree has a CMake cache bound
to `dunecity-quantbot-split`; it is not the integration build.

Successful managed probe runs remove copied `profile/mods` and
`profile/workshop/revisions`; failures retain assets for 72 hours. Logs, settings,
saved games, replays and telemetry evidence remain. The shared staging test
wrapper also tracks changed generated profiles, with a durable before-snapshot
for crash recovery. The daily housekeeping job only deletes registered expired
assets after their owners/process groups have exited.

The four manual profile-copy runners use `probe_profile.managed_profile` on
registered managed-host output roots. Elsewhere they remain standalone Python
scripts; unregistered output paths and CI hosts are not silently adopted by
the Mac cleanup service. Prefer an explicit `--build-dir` when invoking other
standalone probes. CTest passes its configured build directory.

This coordinates cooperating agents, not arbitrary same-user shell programs.
Sessions started before hook/extension installation need a reload/restart and
ownership reconciliation. Preserve the bounded launcher's existing checkout
locks. Its multiple-job path ownership and the external signed CI runner need
separate migration; do not bypass their controls.

Local Homebrew staging does not replace clean signed macOS CI, nor Windows,
Linux, WebAssembly or sanitizer checks. Version once per accepted release batch
and follow `release-operations.md`. Configuration-only bundle refresh and stale
resource removal are known follow-ups from the build audit; keep release checks
until those are addressed and tested.
