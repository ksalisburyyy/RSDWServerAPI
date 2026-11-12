#!/bin/bash
# Foreground launcher for manual use or the THC controller.
set -eu

server_root='/home/thc/dragonwildsserver'
cd -- "$server_root"

if [[ ! -r "$server_root/librsdwapi.so" ]]; then
    echo 'THC: librsdwapi.so is missing or unreadable.' >&2
    exit 1
fi
if [[ ! -x "$server_root/RSDragonwildsServer.sh" ]]; then
    echo 'THC: RSDragonwildsServer.sh is missing or not executable.' >&2
    exit 1
fi

printf '%s\n' 'THC | Starting Dragonwilds Dedicated Server'
exec env LD_PRELOAD="$server_root/librsdwapi.so" "$server_root/RSDragonwildsServer.sh" -log "$@"
