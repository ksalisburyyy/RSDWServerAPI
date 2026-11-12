#include "items.h"
#include "dom_engine.h"
#include "native_call.h"
#include "process_event.h"
#include "../utils/logger.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cstring>

namespace DomItems {

namespace {

using namespace DomEngine;

// UItemData::MaxStackSize - Source: RSDWSDK/CppSDK/SDK/Dominion_classes.hpp:4748
constexpr uintptr_t kMaxStackSize = 0x124;

// DominionRuntimeBlueprintLibrary_TryGiveItemToPlayer - Source: Assertions.inl
struct GiveParams {
    uintptr_t PlayerController;
    uintptr_t ItemData;
    int32_t Count;
    bool ReturnValue;
    uint8_t Pad[3];
};

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return value;
}

uintptr_t g_GiveFunction = 0;
uintptr_t g_GiveLibrary = 0;

bool ResolveGive(std::string& outError) {
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) {
        outError = "engine not initialised";
        return false;
    }
    if (!g_GiveFunction) {
        g_GiveFunction = engine->FindFunction("DominionRuntimeBlueprintLibrary", "TryGiveItemToPlayer");
    }
    if (!g_GiveLibrary) {
        g_GiveLibrary = engine->FindObject("DominionRuntimeBlueprintLibrary", true);
    }
    if (!g_GiveFunction || !g_GiveLibrary) {
        outError = "TryGiveItemToPlayer not found";
        return false;
    }
    if (!NativeCall::IsNative(g_GiveFunction)) {
        outError = "TryGiveItemToPlayer is not native";
        return false;
    }
    const auto shape=(uint32_t)Mem::ReadI32(g_GiveFunction+0xB4);
    if(((shape>>16)&0xffff)!=0x15 || (shape&0xff)!=4) {
        outError = "TryGiveItemToPlayer has an unsupported reflected signature";
        return false;
    }
    if (!GameThread::IsReady()) {
        outError = "give needs the game thread pump: " + GameThread::Status();
        return false;
    }
    return true;
}

}

std::vector<ItemInfo> ListLoaded() {
    std::vector<ItemInfo> items;
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) return items;

    int32_t count = engine->GetObjectCount();
    for (int32_t i = 0; i < count; i++) {
        uintptr_t object = engine->GetObjectByIndex(i);
        if (!object) continue;
        if (!engine->IsA(object, "ItemData")) continue;

        std::string name = engine->GetObjectName(object);
        if (name.empty() || name.rfind("Default__", 0) == 0) continue;

        ItemInfo info;
        info.name = name;
        info.object = object;
        info.maxStackSize = Mem::ReadI32(object + kMaxStackSize);
        items.push_back(info);
    }

    std::sort(items.begin(), items.end(),
              [](const ItemInfo& a, const ItemInfo& b) { return a.name < b.name; });
    return items;
}

uintptr_t FindItem(const std::string& name, std::string& outResolvedName) {
    outResolvedName.clear();
    if (name.empty()) return 0;

    std::string wanted = Lower(name);
    std::string prefixed = wanted.rfind("item_", 0) == 0 ? wanted : "item_" + wanted;

    uintptr_t partial = 0;
    std::string partialName;
    int partialHits = 0;

    for (const ItemInfo& item : ListLoaded()) {
        std::string lower = Lower(item.name);
        if (lower == wanted || lower == prefixed) {
            outResolvedName = item.name;
            return item.object;
        }
        if (lower.find(wanted) != std::string::npos) {
            partialHits++;
            partial = item.object;
            partialName = item.name;
        }
    }

    if (partialHits == 1) {
        outResolvedName = partialName;
        return partial;
    }
    return 0;
}

bool GiveItemToController(uintptr_t controller, uintptr_t itemData, int32_t count) {
    std::string error;
    if (!ResolveGive(error)) return false;
    if (!controller || !itemData || count <= 0 || count > 100000) return false;

    GiveParams params = {};
    params.PlayerController = controller;
    params.ItemData = itemData;
    params.Count = count;

    bool invoked = false;
    bool ran = GameThread::RunSync([&]() {
        invoked = NativeCall::Invoke(g_GiveLibrary, g_GiveFunction, &params, 0x14);
    }, 10000);

    return ran && invoked && params.ReturnValue;
}

bool GiveItem(const std::string& player, const std::string& item, int32_t count,
              std::string& outMessage) {
    Engine* engine = g_Engine;
    std::string error;
    if (!ResolveGive(error)) {
        outMessage = error;
        return false;
    }
    if (count <= 0 || count > 100000) {
        outMessage = "count must be between 1 and 100000";
        return false;
    }

    std::string resolved;
    uintptr_t itemData = FindItem(item, resolved);
    if (!itemData) {
        outMessage = "no loaded item matches '" + item + "'";
        return false;
    }

    uintptr_t controller = 0;
    std::string matched;
    std::string wanted = Lower(player);
    int matches=0;
    for (const PlayerInfo& p : engine->GetAllPlayers()) {
        if (std::to_string(p.playerId) == player || Lower(p.uniqueNetId) == wanted ||
            Lower(p.characterGuid) == wanted || Lower(p.characterName) == wanted ||
            Lower(p.name) == wanted) {
            if(++matches>1) {
                outMessage = "ambiguous player; use a unique network ID or character GUID";
                return false;
            }
            controller = p.controllerPtr;
            matched = p.characterName.empty()?p.name:p.characterName;
        }
    }
    if (!controller) {
        outMessage = "no connected player matches '" + player + "'";
        return false;
    }

    if (!GiveItemToController(controller, itemData, count)) {
        outMessage = "the game refused to give " + std::to_string(count) + " x " + resolved +
                     " to " + matched;
        return false;
    }

    outMessage = "gave " + std::to_string(count) + " x " + resolved + " to " + matched;
    LogMessage("Items: " + outMessage);
    return true;
}

}
