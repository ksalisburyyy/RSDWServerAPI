# Player scan and backup polish — staging validation

This change is prepared on top of the verified 0.5.0-thc.8 source. It is not deployed.

## Behavior

Death telemetry now uses a fresh identity-only enumeration on the game thread.
It retains current player state, character GUID/name and pawn/controller discovery,
but does not fetch unused transforms, skills, health or survival components. The
existing detailed GetAllPlayers path and public player payload remain unchanged.
There is no identity or health cache and no change to death-event validation,
old-pawn protection, deduplication or ledger identity. Other detailed monitoring/API
calls still perform their existing scans; this does not claim to remove every scan
or fix the VM-wide incident.

The new production-code fixture simulates 100,000 loaded objects. Detailed reads
still scan them and return hunger/thirst; identity reads perform zero object
lookups. It also checks fresh names/GUIDs, respawn/despawn, uninitialized/missing
world, unreadable states and empty/oversized player arrays. These are deterministic
operation counts, not a live FPS/CPU improvement measurement.

Server Management shows the count and total size of listed backups and the latest
listed backup time. Empty and unavailable lists have distinct messages. This is
metadata, not a new archive-integrity verification or offsite backup promise.
Malformed metadata, mismatched IDs, missing archives and symlink entries are
excluded from restore choices. No backups are automatically deleted.

Browser fixtures accept CHROMIUM_PATH, a system chromium binary, or Playwright's
bundled browser, removing the old hardcoded /usr/bin/chromium prerequisite.

## Completed checks

- 27 native C++ regression executables passed, including the new enumeration
  fixture and existing capture, cause, ledger, permissions, native command/save,
  role/audit and durable delivery checks.
- The new enumeration fixture passed AddressSanitizer and UndefinedBehaviorSanitizer.
- Nine Python fixture suites passed: control, control_adoption, update, error_watch,
  status_heartbeat, watcher, discord_setup, installer_manager, installer_process.
- Python syntax parsing and the JavaScript map model test passed.
- The full dashboard browser fixture passed, including backup populated/empty/error
  states, roles, token clearing, save request/completion distinction and mobile width.
- The two-browser shared-map fixture passed calibration/image preservation, stale
  revision refusal and persistence after controller-store recreation.

These checks use disposable state, fake processes/APIs and fake notification
transports, not the live game, access keys, saves or Discord destinations.

- The complete native library compiled and linked successfully. Its highest imported
  GLIBC requirement is 2.38, confirming it is not a Debian 12 release artifact.

## Build and release boundary

GCC 12 and browser dependencies were downloaded/unpacked under the chat work
folder; no compiler/browser packages were installed into the VM. Compilation ran
at reduced scheduling priority. The test toolchain targets Ubuntu's host libraries,
not Debian 12: any resulting library is a validation artifact only. A compatible
Debian12/GCC12 release build, maximum GLIBC2.36, static libstdc++/libgcc, checksummed
release manifest and a coordinated gameplay verification window are still required
before deployment. The source version remains .8 because no new release is issued.

The owner withdrew automatic zero-player deployment permission. Do not deploy,
restart the live game or swap libraries on an idle timer. Arrange a proper window.
