#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -g -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -pthread)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"$CXX" "${FLAGS[@]}" tests/http_audit_test.cpp -o build/tests/http_audit_test
build/tests/http_audit_test
"$CXX" "${FLAGS[@]}" tests/recovery_test.cpp -o build/tests/recovery_test
build/tests/recovery_test
"$CXX" "${FLAGS[@]}" tests/health_attributes_test.cpp -o build/tests/health_attributes_test
build/tests/health_attributes_test
"$CXX" "${FLAGS[@]}" tests/command_args_test.cpp -o build/tests/command_args_test
build/tests/command_args_test
"$CXX" "${FLAGS[@]}" tests/event_page_test.cpp -o build/tests/event_page_test
build/tests/event_page_test
"$CXX" "${FLAGS[@]}" tests/sync_queue_test.cpp -o build/tests/sync_queue_test
"$CXX" "${FLAGS[@]}" tests/engine_regressions.cpp src/runtime.cpp -o build/tests/engine_regressions
"$CXX" "${FLAGS[@]}" tests/function_lookup_test.cpp src/runtime.cpp -o build/tests/function_lookup_test
"$CXX" "${FLAGS[@]}" tests/pump_regressions.cpp src/runtime.cpp -ldl -o build/tests/pump_regressions
build/tests/sync_queue_test
build/tests/pump_regressions
build/tests/function_lookup_test
build/tests/engine_regressions

"$CXX" -std=c++17 tests/preload_host.cpp -o build/tests/preload_host
"$CXX" "${FLAGS[@]}" tests/thc_store_test.cpp src/thc/store.cpp -o build/tests/thc_store_test
"$CXX" "${FLAGS[@]}" tests/delivery_test.cpp src/discord/delivery.cpp -o build/tests/delivery_test
build/tests/thc_store_test
build/tests/delivery_test
"$CXX" "${FLAGS[@]}" tests/combat_test.cpp -o build/tests/combat_test
build/tests/combat_test
"$CXX" "${FLAGS[@]}" tests/access_control_test.cpp src/net/access_control.cpp -o build/tests/access_control_test
build/tests/access_control_test
"$CXX" "${FLAGS[@]}" tests/kick_test.cpp src/runtime.cpp -o build/tests/kick_test
build/tests/kick_test
"$CXX" "${FLAGS[@]}" tests/access_roles_test.cpp src/thc/access.cpp src/thc/metrics.cpp -o build/tests/access_roles_test
build/tests/access_roles_test
"$CXX" "${FLAGS[@]}" tests/administration_test.cpp -o build/tests/administration_test
build/tests/administration_test

"$CXX" "${FLAGS[@]}" tests/items_test.cpp src/runtime.cpp -o build/tests/items_test
build/tests/items_test

"$CXX" "${FLAGS[@]}" tests/world_save_test.cpp -o build/tests/world_save_test
build/tests/world_save_test

"$CXX" "${FLAGS[@]}" tests/notification_style_test.cpp -o build/tests/notification_style_test
build/tests/notification_style_test

"$CXX" "${FLAGS[@]}" tests/death_cause_test.cpp src/thc/store.cpp -o build/tests/death_cause_test
build/tests/death_cause_test
"$CXX" "${FLAGS[@]}" tests/death_event_ledger_test.cpp src/thc/store.cpp -o build/tests/death_event_ledger_test
build/tests/death_event_ledger_test

"$CXX" "${FLAGS[@]}" tests/death_capture_test.cpp -o build/tests/death_capture_test
build/tests/death_capture_test

"$CXX" "${FLAGS[@]}" tests/log_damage_test.cpp src/thc/store.cpp -o build/tests/log_damage_test
build/tests/log_damage_test

"$CXX" "${FLAGS[@]}" tests/death_telemetry_test.cpp src/thc/store.cpp -o build/tests/death_telemetry_test
build/tests/death_telemetry_test

"$CXX" "${FLAGS[@]}" tests/environment_damage_test.cpp -o build/tests/environment_damage_test
build/tests/environment_damage_test
