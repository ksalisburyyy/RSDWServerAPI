# THC Management System — 0.5.0-thc.8

Custom Linux RuneScape: Dragonwilds management system, extending RSDWServerAPI
commit `994a72092f77e9851797bbd6206c80a3ebb28e5f`. Current source was recovered
from the matching owner release archive; see [PROVENANCE.md](PROVENANCE.md).
Keep [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Components and capabilities

- C++17 `LD_PRELOAD` library (`librsdwapi.so`): native HTTP API, optional Source
  RCON, player data, game admin commands, save completion observation, persistent
  character/event ledger, death telemetry and Discord queues.
- Independent Python standard-library controller: authenticated dashboard/proxy,
  private Unix control socket, configured game start/stop/restart, offline save
  backup/restore, narrow INI edits, shared map and fixed SteamCMD update jobs.
- Independent watcher: current invite/process status, severe crash evidence,
  private admin audit delivery, retry queues and 45-minute online/invite repost.
- Dashboard: embedded in `src/api/dashboard.h`; artwork in `assets/`. File-backed
  JSON/INI/NDJSON storage and polling; no SQL, WebSocket or SSE dependency.

Version .8 preserves verified credential labels/IDs/roles in audits, snapshots
async job identity, ignores public actor claims, and deduplicates native/proxy
Discord audit delivery without dropping the private proxy record. Named keys
identify the credential used, not a person independently of its possession.

The game owns `/help`, `/kick`, `/ban`, `/unban`, `/give` authorization. Commands
support quoted arguments; give quantities are 1..100000 and ambiguous ban/unban
names are refused. Five Discord lanes cover chat, kills/deaths, player activity,
status and admin actions. Routine save notifications remain enabled when configured.
Weather-cache warnings and ordinary Error/Warning lines are excluded from crash posts.

See [THC_API.md](THC_API.md), [CONTROL_SERVICE.md](CONTROL_SERVICE.md),
[MAP_SETUP.md](MAP_SETUP.md), [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and the historical
[THC_CHANGES.md](THC_CHANGES.md). Earlier change notes describe their release-time
status; the current limits and provenance documents take precedence.

## Build

Use Debian 12 / GCC 12 with static libstdc++ and libgcc and maximum GLIBC 2.36.
The preserved `Dockerfile.build` and `Makefile` provide this compatibility:

```bash
bash build.sh -j4
```

Output: `dist/librsdwapi.so`. A host `make` is suitable only when its compiler and
sysroot meet the same compatibility target. Do not deploy a library built on an
arbitrary newer host. Verify ELF dependencies and symbol versions before release.

## Installation and configuration

Plan installation separately from source synchronization. Back up the current
library, management files and private configuration. Stop the game before native
library replacement. Never extract source or build output over the live game tree.
A typical launch uses:

```bash
LD_PRELOAD=/path/to/librsdwapi.so /path/to/RSDragonwildsServer-Linux-Shipping -log
```

Native settings are under `RSDragonwilds/Binaries/Linux/rsdwapi/settings.ini` by
default, or the `RSDWAPI_DIR` directory. Keep the existing settings and `thc/access.json`
to preserve credentials. API requests use `Authorization: Bearer <token>`; keep
listeners on localhost behind the authenticated controller/TLS proxy. RCON is optional.
Do not commit live INI, access files, webhooks, saves, logs or private TLS/SSH keys.

Review examples under `tools/` for controller configuration and user services.
Deployment copies the controller/watcher beside `dashboard.html` and both artwork
files; the controller serves those images at `/assets/`. Generate dashboard HTML
from the raw string in `src/api/dashboard.h`; it is not a second maintained source.
`tools/install_death_ledger.py` is the matching release-package installer, not a
standalone source-checkout deployment command; it requires verified package assets
and manifest. No installer should run during repository synchronization.

## Validation

```bash
bash tests/run.sh                     # C++ fixture regressions; needs compiler
python3 tests/control_test.py         # temporary saves/config, fake game and API
python3 tests/error_watch_test.py      # fixture logs and fake notification transport
node tests/map_model_test.js
```

Other Python tests under `tests/` cover update interlocks, process adoption,
installers and watcher behavior. `smoke.py`, `named_audit_e2e.py` and
`death_installer_test.py` additionally require `dist/librsdwapi.so`; use a compatible
build and fixture host from `tests/run.sh`. They use disposable state/fake transport,
not production. Browser tests require Playwright plus Chromium at `/usr/bin/chromium`.
See [VALIDATION.md](VALIDATION.md) for this synchronization's actual results.

Release binaries and ZIP packages belong in versioned release assets, with matching
source, manifests and checksums, rather than source commits. This synchronization
does not deploy or publish a newly built binary.
