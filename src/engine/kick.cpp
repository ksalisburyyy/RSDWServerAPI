#include "dom_engine.h"
#include "native_call.h"
#include "process_event.h"
#include "../utils/logger.h"
#include "../utils/memory.h"

#include <cstring>
#include <codecvt>
#include <locale>
#include <vector>

namespace DomEngine {

namespace {

// Source: RSDWSDK/CppSDK/SDK/RedpointEOSFramework_parameters.hpp:483
struct KickParams {
    uintptr_t PlayerController;  // 0x00
    uint8_t   KickReason[0x10];  // 0x08, FText
    bool      ReturnValue;       // 0x18
    uint8_t   Pad[0x7];
};

// Source: RSDWSDK/CppSDK/SDK/Engine_parameters.hpp:45946
struct ConvStringToTextParams {
    FString InString;            // 0x00
    uint8_t ReturnValue[0x10];   // 0x10, FText
};

void WriteFString(FString& out,const std::u16string& value) {
    out.Data=const_cast<char16_t*>(value.c_str());
    out.Num=(int32_t)value.size()+1;out.Max=out.Num;
}

}

bool Engine::KickPlayer(const std::string& identifier, const std::string& reason,
                        std::string& outError) const {
    if (!initialized) {
        outError = "engine not initialised";
        return false;
    }

    std::string PlayerInfo::* const fields[] = {
        &PlayerInfo::uniqueNetId,
        &PlayerInfo::characterGuid,
        &PlayerInfo::characterName,
        &PlayerInfo::name,
    };

    std::vector<PlayerInfo> players = GetAllPlayers();

    uintptr_t controller = 0;
    std::string matched;
    std::string matchedBy;

    for (const PlayerInfo& player : players) {
        if (std::to_string(player.playerId) != identifier) continue;
        controller = player.controllerPtr;
        matched = player.name;
        matchedBy = "playerId";
        break;
    }

    for (size_t f = 0; !controller && f < sizeof(fields) / sizeof(fields[0]); f++) {
        int hits = 0;
        for (const PlayerInfo& player : players) {
            if ((player.*fields[f]).empty()) continue;
            if (player.*fields[f] != identifier) continue;
            hits++;
            controller = player.controllerPtr;
            matched = player.name;
            }

        if (hits > 1) {
            outError = "'" + identifier + "' matches " + std::to_string(hits) +
                       " players; kick by netId or playerId instead";
            return false;
        }
        if (hits == 1) {
            const char* names[] = {"netId", "characterGuid", "characterName", "name"};
            matchedBy = names[f];
        }
    }

    if (!controller) {
        outError = "no connected player matches '" + identifier + "'";
        return false;
    }
    LogVerbose("Kick: matched '" + matched + "' by " + matchedBy);

    uintptr_t kickFunction = FindFunction("RedpointFrameworkBlueprintLibrary",
                                          "KickPlayerController");
    if (!kickFunction) {
        outError = "KickPlayerController function not found in GObjects";
        return false;
    }

    uintptr_t kickLibrary = FindObject("RedpointFrameworkBlueprintLibrary", true);
    if (!kickLibrary) {
        outError = "RedpointFrameworkBlueprintLibrary default object not found";
        return false;
    }

    uintptr_t textFunction = FindFunction("KismetTextLibrary", "Conv_StringToText");
    uintptr_t textLibrary = FindObject("KismetTextLibrary", true);

    if (!NativeCall::IsNative(kickFunction)) {
        outError = "KickPlayerController is not a native function";
        return false;
    }

    KickParams params = {};
    params.PlayerController = controller;

    bool haveReason = false;

    if (!GameThread::IsReady()) {
        outError = "kick needs the game thread pump: " + GameThread::Status();
        return false;
    }

    bool invoked = false;
    bool ran = GameThread::RunSync([&]() {
        if (textFunction && textLibrary && NativeCall::IsNative(textFunction)) {
            ConvStringToTextParams textParams = {};
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> converter;
            std::u16string reasonText=converter.from_bytes(reason);
            WriteFString(textParams.InString,reasonText);
            // Source: RSDWSDK/CppSDK/SDK/Engine_parameters.hpp:45950
            if (NativeCall::Invoke(textLibrary, textFunction, &textParams, 0x10)) {
                memcpy(params.KickReason, textParams.ReturnValue, sizeof(params.KickReason));
                haveReason = true;
            }
        }
        if (!haveReason) return;
        // Source: RSDWSDK/CppSDK/SDK/RedpointEOSFramework_parameters.hpp:488
        invoked = NativeCall::Invoke(kickLibrary, kickFunction, &params, 0x18);
    }, 10000);

    if (!ran) {
        outError = "the game thread did not run the kick within 10s";
        return false;
    }
    if (!haveReason) {
        outError = "could not build the kick reason text";
        return false;
    }

    if (!invoked) {
        outError = "could not invoke KickPlayerController";
        return false;
    }

    if (!params.ReturnValue) {
        outError = "the game refused the kick for '" + matched + "'";
        return false;
    }

    LogMessage("Kick: removed '" + matched + "' (" + reason + ")");
    return true;
}

}
