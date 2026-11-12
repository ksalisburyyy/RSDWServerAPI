#include "dom_engine.h"
#include "../utils/memory.h"
#include "../utils/logger.h"

namespace DomEngine {

// Exact-owner-first / derived-owner fallback adapted from World Warden
// 2.0.1 engine/ufunction.cpp, copyright 2026 TupeloApache, MIT.
// Linux keeps its own UObject offsets. Unlike the Windows implementation,
// ambiguous fallback matches are refused instead of choosing the first one.
uintptr_t Engine::FindFunction(const char* className, const char* functionName) const {
    if (!className || !functionName) return 0;
    const std::string wanted = functionName;
    const int32_t count = GetObjectCount();
    for (int pass = 0; pass < 2; ++pass) {
        uintptr_t found = 0;
        for (int32_t i = 0; i < count; ++i) {
            uintptr_t object = GetObjectByIndex(i);
            if (!object || GetObjectClassName(object) != "Function" ||
                GetObjectName(object) != wanted) continue;
            uintptr_t outer = Mem::ReadPtr(object + Offsets::UObject_Outer);
            bool matches = false;
            if (pass == 0) {
                matches = GetObjectName(outer) == className;
            } else {
                for (int depth = 0; outer && depth < 32; ++depth) {
                    if (GetObjectName(outer) == className) { matches = true; break; }
                    outer = Mem::ReadPtr(outer + Offsets::UStruct_SuperStruct);
                }
            }
            if (!matches) continue;
            if (found && found != object) {
                LogMessage("Function lookup: ambiguous " + std::string(className) + "." + wanted);
                return 0;
            }
            found = object;
        }
        if (found) return found;
    }
    return 0;
}

}
