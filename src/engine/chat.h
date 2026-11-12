#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace DomChat {

struct Message {
    std::string senderName;
    std::string senderNetId;
    std::string characterGuid;
    int32_t playerId = 0;
    std::string body;
};

struct PlayerEvent {
    std::string senderName;
    std::string senderNetId;
    std::string characterGuid;
    int32_t playerId = 0;
    std::string tag;
};

using Listener = std::function<void(const Message&)>;
using EventListener = std::function<void(const PlayerEvent&)>;

bool Initialize();
bool IsHooked();
std::string Status();
bool CanBroadcast();

void AddListener(Listener listener);
void AddEventListener(EventListener listener);

void Poll();

bool Broadcast(const std::string& body, std::string& outError);

}
