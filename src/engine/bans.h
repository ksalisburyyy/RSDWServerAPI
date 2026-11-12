#pragma once
#include <string>
#include <vector>

namespace DomBans {

struct BanEntry {
    std::string netId;
    std::string name;
    std::string reason;
    std::string bannedAt;
};

void Load();
bool IsReady();
std::vector<BanEntry> All();
bool IsBanned(const std::string& netId);

bool Ban(const std::string& player, const std::string& reason, std::string& outMessage);
bool Unban(const std::string& identifier, std::string& outMessage);

void Enforce();

}
