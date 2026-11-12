set_project("rsdw-serverapi")
set_version("0.5.0")

add_rules("mode.release")
set_languages("c++17")

-- Linux only: the target is RSDragonwildsServer-Linux-Shipping, and the mod is
-- loaded with LD_PRELOAD rather than a proxy DLL.
target("rsdwapi")
    set_kind("shared")
    add_files("src/rsdwapi.cpp",
              "src/runtime.cpp",
              "src/thc/store.cpp",
              "src/thc/metrics.cpp",
              "src/thc/control.cpp",
              "src/thc/access.cpp",
              "src/engine/administration.cpp", "src/engine/world_save.cpp",
              "src/thc/service.cpp",
              "src/thc/combat.cpp", "src/thc/death_telemetry.cpp",
              "src/discord/delivery.cpp",
              "src/api/thc_routes.cpp",
              "src/api/api_routes.cpp",
              "src/api/system_routes.cpp",
              "src/api/player_routes.cpp",
              "src/api/serialize.cpp",
              "src/engine/engine_core.cpp",
              "src/engine/uobject.cpp",
              "src/engine/players.cpp",
              "src/engine/function_lookup.cpp",
              "src/engine/kick.cpp",
              "src/engine/chat.cpp",
              "src/engine/items.cpp",
              "src/engine/bans.cpp",
              "src/discord/webhook.cpp",
              "src/engine/native_call.cpp",
              "src/engine/process_event.cpp",
              "src/engine/server.cpp",
              "src/http/http_server.cpp",
              "src/net/access_control.cpp",
              "src/rcon/rcon_server.cpp",
              "src/rcon/rcon_commands.cpp",
              "src/rcon/commands_system.cpp",
              "src/rcon/commands_players.cpp",
              "src/utils/memory.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_filename("librsdwapi.so")
    set_targetdir("dist")
    add_syslinks("pthread", "dl")
