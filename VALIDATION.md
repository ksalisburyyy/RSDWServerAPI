# Synchronization validation

Validated in an isolated staging checkout; production was not modified.

- Python AST parsing passed for all tools and tests.
- Nine Python fixture suites passed: control, control_adoption, update,
  error_watch, status_heartbeat, watcher, discord_setup, installer_manager,
  installer_process. Disposable state, fake processes/services and injected
  notification transports were used; no live game/API writes or Discord posts.
- JavaScript map_model_test passed.
- Controller, watcher and embedded dashboard source exactly match deployed .8
  manifest hashes. Both approved artwork files are present.
- Current source and existing Git history were scanned for exact deployed native
  token/password/webhook values and recognizable private-key/GitHub-token/webhook
  patterns. No matches found. Fake test credentials remain legitimate fixtures.
  This targeted scan is not a guarantee against every possible secret format.

Native build/C++ regression suites were not run: this host has no C++ compiler,
make or Docker. Browser suites were not run: Playwright/Chromium are unavailable.
Smoke, named-audit E2E and native installer integration suites require compatible
build artifacts and were not run. No new native binary or release was published.
Use Debian12/GCC12, maximum GLIBC2.36 and static libstdc++/libgcc for future builds.
Prior owner gameplay confirmation is described separately in KNOWN_ISSUES.md.
