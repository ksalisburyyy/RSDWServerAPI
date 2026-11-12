## 0.5.0-thc.8 additions

`GET /api/thc/access/me` now returns `id`, `label`, and `role`, without the token secret. HTTP and control audits use that authentication-time identity. Native queued audits add `X-THC-Audit-Queued: 1` to the response; the controller retains a private duplicate record marked `nativeAuditQueued`, and its watcher skips only that extra Discord delivery. Request headers/body cannot assert this acknowledgement or choose the recorded actor. Control jobs retain their initiating credential snapshot even after subsequent logins or revocation.

## 0.5.0-thc.7 additions

`POST /api/thc/recheck` (admin, JSON `{}`): returns HTTP 202 and a queued/coalesced recovery request when the engine is initialized, otherwise 503. The monitoring loop retries existing validated discovery and reports `compatibility.recovery` in `/api/thc/status`. A checked request means the attempt ran, not that gameplay is confirmed. No save, damage, kick, teleport, restart or arbitrary invocation is requested.

`GET /api/thc/events?latest=1&limit=500`: newest retained window in chronological order. `latest` accepts true/false or 1/0 and cannot be combined with `after`. Existing `after` requests keep chronological cursor paging. Responses add `nextAfter` and `hasMore`.

In-game `/help`, `/kick`, `/ban`, `/unban`, `/give` now support quoted names and emit one native audit per recognized command attempt. Game owner/admin authorization is unchanged. RCON uses the same argument parser; give counts are strict whole numbers 1..100000 on every command surface.

# THC HTTP additions

All /api routes use the existing Authorization: Bearer credential. The owner
credential is admin. Added tokens support viewer, operator and admin roles.
The HTML shell /dashboard contains no secrets and requires login for API data.
Existing RSDWServerAPI routes remain documented in README.md.

| Method | Path | Purpose |
|---|---|---|
| GET | /api/thc/status | Discovery, ledger, Discord and system telemetry |
| GET | /api/thc/characters | Retained GUID character records |
| GET | /api/thc/events | Retained events |
| GET | /api/thc/analytics | 24 hourly buckets over retained events |
| GET | /api/thc/access/me | Current role |
| GET | /api/thc/access | Redacted additional token list (admin) |
| POST | /api/thc/access/create | label, role; token returned once (admin) |
| POST | /api/thc/access/revoke | id (admin) |
| POST | /api/thc/teleport | player, x, y, z (operator/admin) |
| POST | /api/thc/console | command (admin) |
| GET | /api/thc/save | Native save readiness, request state and observed completion |
| POST | /api/thc/save | Request native world save; acceptance is separate from completion (admin) |
| GET | /api/thc/functions | search, after query; read-only native metadata (admin) |
| GET/POST | /api/thc/settings | Redacted mod config; atomic updates (admin) |
| GET | /api/thc/control | Optional agent status and job results (admin) |
| GET | /api/thc/control/logs | Agent real log tail (admin) |
| GET | /api/thc/control/backups | Backup metadata (admin) |
| GET | /api/thc/control/game-settings | Existing supported INI keys (admin) |
| POST | /api/thc/control | action; accepted async job (admin) |

Control actions: start, stop, restart, backup, restore (id),
update_game_settings (values object). Poll GET control for job completion; acceptance
is not completion. The standalone agent mirrors these routes on localhost5001
and forwards native routes to the mod, preserving caller authorization.

Viewer is read-only and cannot read privileged config/control/token inventory.
Operator additionally permits kick, item giving, broadcast and teleport.
Unsupported game capabilities return failure rather than simulating execution.
These roles do not grant or revoke in-game administrator membership or RCON access.

Native saves return HTTP 202 when accepted. States include pending, reported_success,
reported_failure, not_invoked and unconfirmed. The current game-thread pump must
have been observed recently; no headless-tick capability is claimed. Discord settings
accept chatThreadId, deathThreadId, presenceThreadId, statusThreadId and adminThreadId
as exact strings; stored webhook credentials are never returned.
