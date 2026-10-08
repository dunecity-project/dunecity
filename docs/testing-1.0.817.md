# Testing the local spectator candidate (1.0.817)

This candidate adds **Spectate** to Offline Custom Game. The viewer uses no faction or
starting position, sees the entire map and minimap, and can inspect every house's buildings.
The AI controls every active faction. Production and the currently running MBA app remain
1.0.816; that app does not have this new checkbox yet.

The rebuilt native candidate is at
`/Users/stefan/Documents/projects/dunecity-pr89-test/build/bin/dunecity.app` on claw.local.
It is a local development build, using this host's Homebrew libraries.

## Try the map from your screenshot

1. Launch **1.0.817**, choose **Custom Game**, and select **Offline**.
2. Choose **Dune City 1.002** and **2P - 128x128 - 1v1 - Tuono-Orac**.
3. Check **Spectate** beside **Shared house**. Your name is replaced by an AI; you do
   not occupy either of the two houses.
4. Set the first row to **Atreides / Team 1 / QuantBot Easy**, and the second to
   **Harkonnen / Team 2 / QuantBot Easy**. Keep **Spice 1x** and **SimCity** graphics.
5. Choose **Start Game**. The screen says **Spectating**, and the map and minimap are
   visible from the start. Give the AIs time to establish their bases.
6. Left-click buildings belonging to both colours. Windtraps show the selected owner's
   power figures; factories show that owner's production and upgrade progress; city
   buildings show their city details. Scroll factory lists to inspect offscreen entries.
7. Try clicking production entries or giving move/attack orders. The viewer cannot
   issue them. The AI should continue building and spending its own credits.
8. Use **Esc → Save Game**, then **Load Game**. Full visibility and read-only inspection
   remain. **Restart Game** after loading also starts another observed all-AI match.

For immediate building inspection, use **4P - 192x192 - Metropolis** with at least two AI
houses on different teams. This map has established cities. Unchecking **Spectate** in
setup restores a human seat. Choosing **Online** clears and hides the offline choice.

## Verification

Four focused behaviours are exercised against production engine objects: the local viewer,
ordinary human save/load, the actual setup widgets, and continuation of existing saves.
The spectator fixture verifies real AI progress, a working minimap without a powered radar,
owner-specific figures, rejected orders and mutation cheats, unchanged faction exploration,
and save/reload/restart. Existing command tests include real-owner positive controls.

The first full 62-gate CTest run found four failures when opening network-format saves locally.
Those saves omit the local-player ID byte. The corrected loader restores an actual human
controller by name, or a deterministic human fallback. The four failing continuation probes
then passed with the spectator and native regression checks.

Logs and screenshots are retained in
`/Users/stefan/Documents/projects/outputs/spectator-mode-817/`.
