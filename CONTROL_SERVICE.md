# Controller and deployment architecture

`tools/thc_control.py` reads a private JSON configuration (examples in `tools/`).
Paths for game executable, saves, logs, native API settings, dashboard, backups and
Unix socket must be reviewed for the actual installation. The controller is
independent of the game and can serve the authenticated dashboard while it is offline.
The game/native API runs as the deployment owner; the private socket checks peers.

Use `thc-control.service.example` and `thc-error-watch.service.example` as reviewed
user-service templates. Preserve controller `KillMode=process`: restarting the
controller must not kill its game child. The optional boot helper is a oneshot,
not a recurring crash watchdog. No service is enabled by source synchronization.

Start completion proves process launch, not joinability. Restart is graceful
stop/start without an explicit forced Save World first. Backups/restores operate
on the configured save directory while the game is offline, not the whole VM.
SteamCMD update requires a stopped game and verified save backup, uses fixed argv,
blocks start after failure until successful retry, and leaves the game stopped.
Prepared dashboard updates are static local HTML/artwork plus checksums, not an
internet update feed or arbitrary code uploader.

Public HTTPS terminates at an independently configured reverse proxy. There is
no native TLS. Protect settings.ini, access.json, watcher state, audit logs and
webhooks; retain named keys across upgrades. Never paste private config into Git.
