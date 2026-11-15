# 0.5.0-thc.10 — restore player health and display rest

Deployed follow-up to the .9 optimization; live health and rest readings verified.

## Problem and behavior

During normal gameplay, the public player API returned null health although the
server's shared attributes held Health=210 and MaxHealth=210. Read-only inspection
of that player's components and reflected properties confirmed obsolete pawn member
offsets in the existing reader. The raw health component's health fields were both
zero; they are not a valid replacement for the player's named attribute values.

Detailed player enumeration now finds player-health and attribute components by
class and owning pawn during its existing component scan. It reads current/max HP
from the named attributes. Missing, ambiguous or inconsistent components/arrays do
not produce a false zero-health reading or a death. The health component is optional
for attribute availability; it supplies the existing canDie field when present.

Rest was absent from the existing player schema and dashboard. Live reflection
identified EnduranceComponent.CurrentEndurance at +0x130 and MaxEndurance at +0xE0;
the observed rest reading was about 89.8/100. Its +0x134 field is a decay buffer,
not the maximum used by food/water components. The API adds rest as either
{current,max} or null, and both dashboard player tables show Rest. Zero is valid;
nonfinite/out-of-range values and ambiguous components remain unavailable.

Identity-only death-telemetry enumeration still skips this entire component scan.
No extra world scan, sampled-health death gate, inventory modification, game setting
change, fake player value, save replacement or automated restart is introduced.

## Validation and deployment boundary

The production-code fixture covers typed and pawn-owned HP=210, rest=89.8/100,
zero rest, missing/wrong-owner/duplicate components, inconsistent or unreadable
attribute arrays, invalid floats, no false death, fresh identities, lifecycle changes
and the unchanged zero-component-scan identity path over 100,000 objects.
It also exercises the actual JSON serializer. ASan/UBSan passed. The dashboard
browser fixture passed HP/rest values in both tables, zero/unavailable rest and
existing permissions, backups, map, save-state and responsive-layout checks.

All 27 native regression executables passed again. The compatible library passed
68 loader/REST/RCON/persistence/shutdown fixture checks on loopback. The updated
player/serializer fixture also passed when rebuilt with the release compiler.

The complete library was rebuilt with the isolated Debian bookworm/GCC12 toolchain,
static C++ runtimes and maximum imported GLIBC2.36. Native SHA256:
b28473786c12d030e371f8f626a437bb682064a68f7e98eace4d761bc2b415d4.
Verify the package manifest before installation. The owner subsequently authorized a restart as the sole connected player. The game
confirmed a manual world save before the clean stop. Save backup
20261115071425-541bae2d and a separately verified private snapshot of current saves,
configuration, access/map/history and previous deployed code completed first.
Only the library and dashboard were installed; controller/watcher code was retained.
Original permissions and ACLs were restored. Protected files matched before startup.

The restarted game reports .10 with a healthy engine. After the owner spawned,
the actual native API returned health 210/210, rest 88.267/100 and valid food/water.
HTTPS /dashboard matched the .10 manifest, including both Rest columns, with
no-store headers. An already open browser tab needed refreshing to load the new
page; its polling had already picked up the new health values. All four historical
character IDs and nondecreasing counters were verified. Native command/save/death
integration readiness was rechecked after joining; readiness does not prove every
gameplay action or cause. No inventory action, forced death or teleport was tested.
Keep the private backups and installer rollback copies. Never replace a loaded
library or roll back saves merely to undo code changes.
