THC Management System is based on RSDWServerAPI, commit
994a72092f77e9851797bbd6206c80a3ebb28e5f, copyright 2026 Kozejin (MIT).
Upstream: https://github.com/dkoz/RSDWServerAPI

The exact-owner-first / derived-owner UFunction lookup in
src/engine/function_lookup.cpp is adapted from the user-supplied World Warden
2.0.1 source, engine/ufunction.cpp, copyright 2026 TupeloApache (MIT).
Reference: https://www.nexusmods.com/runescapedragonwilds/mods/795
The accompanying LICENSE preserves both copyright notices.

World Warden's Windows DLLs, executable offsets, deployment scripts, historical
operator directives, and private-server configuration are not part of this build.

nlohmann/json 3.11.3 is vendored as src/vendor/nlohmann_json.h, copyright
2013-2023 Niels Lohmann, MIT; its license is src/vendor/JSON_LICENSE.MIT.
There is no runtime JSON package to install.

World Warden 2.0.1 also informed reflected damage-parameter discovery and
evidence-based NPC-health correlation. Its earlier system-message broadcast
fallback has been removed; text broadcasts follow the original RSDW contract.
These have Linux-specific implementations here. Warden's Windows machine-code
patching is not included; the notices in LICENSE cover the adapted portions.

The exact creature class/display-name pairs in src/thc/creature_names.h are
extracted from the MIT-licensed owner-supplied World Warden 2.0.1
src/kills/mob_names.cpp table. Original Warden attribution and license notices
above continue to apply; no Windows pointer/layout constants are imported.
