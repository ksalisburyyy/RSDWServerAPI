# 0.5.0-thc.10 — restore player health and display rest

Prepared follow-up to the deployed .9 optimization; not installed into the live game.

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
Verify the package manifest before installation. The running server remains .9 until a coordinated clean stop and
save backup; never replace the loaded library during a player's session. Installation
and ordinary gameplay confirmation of the new reader are separate from fixture
validation. Keep the existing private .9 backup/rollback artifacts and runtime data.
