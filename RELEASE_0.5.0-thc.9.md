# 0.5.0-thc.9 — player enumeration and backup polish

Includes the optimization and UI/controller changes documented in POLISH_VALIDATION.md.
The original Ubuntu validation build is not used for this release. The deployable
library was rebuilt in an isolated official Debian bookworm image using GCC 12.2,
with static libstdc++/libgcc; its highest imported GLIBC version is 2.36.

Critical player-identity and full death-telemetry fixtures were recompiled with the
release compiler and passed. The prior 27 native regressions, sanitizer fixture,
nine Python suites and both browser suites are recorded in POLISH_VALIDATION.md.

The owner subsequently authorized this specific maintenance deployment while no
players were online. The native API confirmed an engine-ready, zero-player state
immediately before a clean stop. A separately verified save ZIP and a private full
game/project/configuration archive were created before replacing deployed files.
Critical saves, configuration and runtime-state bytes were verified in the archive.
The archive contains private data and must never be committed or publicly uploaded.

The native library, dashboard and controller were installed with the existing
stopped-game/checksummed installer. Original permissions and ACL metadata were
retained. The watcher, artwork and existing game settings were not replaced.
After startup, native health reported version .9 with engineReady and chatHooked
true; the dashboard and authenticated backup listing returned HTTP 200. Credentials,
controller/native settings and shared map were unchanged byte-for-byte. All four
prior ledger characters remained present and historical counters did not regress.

Game-thread-dependent features may await a player joining; startup checks do not
constitute a complete gameplay validation of give/save/commands/death attribution.
Verify those during normal play before treating the release as fully live-tested.

This release does not claim to fix the independent VM/host execution stalls or the
game's longstanding compiled weather-cache limit. Other detailed player reads still
perform their existing object scans.

Rollback: preserve the private pre-thc9 archive and installer's before-thc8-* copies
(the latter suffix is historical installer naming). Stop the game cleanly before
restoring the previous library/dashboard/controller. Do not restore old saves merely
to roll back code. Recheck hashes and start normally; never replace a loaded library.
