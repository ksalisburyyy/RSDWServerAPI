# Historical release notes

These notes retain release-time status. See README.md, KNOWN_ISSUES.md and
PROVENANCE.md for the current .8 scope and verification limits.

## 0.5.0-thc.8

- HTTP authentication retains the verified credential ID, label and role for audits. The header identifies the current credential. Owner credentials are labelled Owner; named keys use their saved labels.
- Native HTTP audits carry structured action details and never copy access-create secrets or sensitive settings/console arguments into audit posts.
- Native API confirms when it queued the audit; controller retains its private copy with a server-generated marker and the watcher skips that redundant Discord delivery. If native delivery is unavailable, the controller/watcher supplies the labelled fallback.
- Asynchronous control jobs snapshot their initiating identity. Public request-body actor claims are ignored; direct native control calls supply the verified identity over the private Unix transport.
- Existing token IDs and secrets remain unchanged. Older generic audit posts cannot be retroactively attributed.

## 0.5.0-thc.7

- Compatibility & Recovery: readiness table, admin-only asynchronous retry of existing validated discovery paths, downloadable redacted diagnostic report. This does not invent offsets or guarantee compatibility with an unknown game build.
- Recent feed uses explicit newest-event paging; ordinary API after-cursors keep their ordering.
- Fresh controller state and busy/update interlocks; stale login refresh responses ignored.
- Shared quote-aware chat/RCON parser, strict 1..100000 give quantities, per-attempt in-game command audit with bound sender identity.
- Ambiguous online ban and stored unban names refused; new bans prefer character names.
- MaxHealth-only/missing/nonfinite Health does not imply death. The live-verified .6 engine-death ledger path remains intact.
- Installer deploys matching native/UI/controller files with verified backups and activation-failure rollback, retaining the process check that worked on the VM.

# THC Management System — candidate 0.5.0-thc.1

Baseline: owner-tested 0.4.0-thc.3, extending RSDWServerAPI
994a72092f77e9851797bbd6206c80a3ebb28e5f. No commits or deployments were made.
The prior working binary/package remain available separately for rollback.

## Native world save

The owner's complete live catalog contains PersistenceSubsystem.SaveGame (one
byte, one parameter) and PostSaveWorldState (17 bytes, two parameters). Public
reflected declarations identify these as SaveGame(bool bAdditionalLogging) and
PostSaveWorldState(FString SlotName, bool bSuccess). The new path finds a unique
non-default subsystem owned by a GameInstance, rechecks its identity on the game
thread, requests a save and observes the original completion callback. It never
writes the world's save format itself or substitutes player-character saves.

POST /api/thc/save returns acceptance separately from completion. GET save and
System → World Save show pending, reported_success, reported_failure, not_invoked
or unconfirmed. A 30-second timeout is unconfirmed, never success. Callbacks are
observations after a request, not a unique game-supplied request ID. Existing
autosaves are observed too. Events and configured status notifications use the
game's reported result. The original callback is always forwarded unchanged.

Current limitation: the existing stripped-Linux game-thread pump depends on
player-controller activity. Save requires a pump observation within ten seconds;
join/move in-game and allow discovery before testing. This is not an always-active
headless tick pump. An empty server may show save unavailable; no off-thread or
guessed cheat invocation is used. A separately verified configured console save
command remains a fallback. Runtime save behavior still needs owner testing.

## Persistent map and destinations

Map images and calibration persist in IndexedDB for this browser/dashboard
address, with no credentials stored there. Image aspect ratio is preserved.
Three pairs of world/image landmarks fit scale, rotation, axes and offset; known
coordinate bounds remain available. Image-only maps do not display uncalibrated
player markers. Singular or tightly clustered landmarks are refused. Map clicks
use the inverse transform for X/Y and do not guess terrain height. Landmark XYZ
can be copied from a live player, saved and selected for later review/teleport.
No wiki tiles, third-party map artwork or unverified RSDWTools bounds are bundled.
Maps are not synchronized across devices or different dashboard ports.

## Discord routes

Five optional thread-ID fields are saved separately from webhook secrets. IDs
remain exact strings end to end. Explicit IDs replace existing thread query
values; blank preserves legacy URL routing. The THC plan can fill unconfigured
fields without changing an already-configured chat destination. Webhook URLs
remain private and are never echoed back. A webhook must belong to the intended
thread's parent channel. The supplied Admin ID is an intended destination, not
remotely verified to be a thread. Saving applies on a planned game restart.
No real Discord test messages were sent; routes are not activated by this package.

## Optional controller

The owner's process output places the game in an SSH session. Separate
thc-dashboard.service and thc-discord.service units were observed and are not game
control targets. The updated Python service can explicitly opt into managing one
matching game process using a stable Linux pidfd, exact executable and UID checks.
It refuses ambiguous processes, duplicate starts, non-game THC units and forced
kills. Process output is a private regular file so the game does not lose stdout
when the controller exits. The example service leaves game children running when
the controller service stops. Owned stale sockets are recovered under a single-agent
lock; active listeners and non-socket files are preserved. Read-only preflight and
a THC-specific configuration draft are supplied. They are not installed or enabled.
Backups/restore remain offline-only; SIGTERM/exit alone is not proof of a world save.

## Verification limits

Owner-verified on thc.3: broadcast display, item delivery, kick/rejoin, incoming
chat-to-Discord, dashboard access and changing live coordinates. Those facts do
not automatically verify this candidate's new native save or optional controller.
Local tests use synthetic engine objects, mock transports, private toy processes
and loopback sockets. They do not run the proprietary game or real Discord.
Map alignment needs actual landmarks; teleport needs the owner's game test.
Complete combat/death attribution is not yet proved across all paths. The supplied
weather-cache warning is open (KNOWN_ISSUES.md); it has not been suppressed or
patched without evidence. Your live VPS was not accessed or changed.
