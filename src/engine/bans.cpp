#include "bans.h"
#include "dom_engine.h"
#include "process_event.h"
#include "../config/config.h"
#include "../utils/json.h"
#include "../utils/atomic_file.h"
#include "../utils/logger.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <set>
#include <mutex>
#include <sstream>

namespace DomBans {

namespace {

using namespace DomEngine;

std::mutex g_Mutex;
std::vector<BanEntry> g_Bans;
bool g_Loaded=false;

std::string Path() {
    return GetBaseDir() + "/bans.json";
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return value;
}

std::string Now() {
    time_t now = time(nullptr);
    struct tm tmval;
    gmtime_r(&now, &tmval);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmval);
    return buf;
}

bool SaveLocked() {
    std::ostringstream file;

    file << "[\n";
    for (size_t i = 0; i < g_Bans.size(); i++) {
        const BanEntry& ban = g_Bans[i];
        file << "  {\"netId\":\"" << Json::Escape(ban.netId)
             << "\",\"name\":\"" << Json::Escape(ban.name)
             << "\",\"reason\":\"" << Json::Escape(ban.reason)
             << "\",\"bannedAt\":\"" << Json::Escape(ban.bannedAt) << "\"}";
        if (i + 1 < g_Bans.size()) file << ",";
        file << "\n";
    }
    file << "]\n";
    std::string error;
    if (!WriteFileAtomically(Path(), file.str(), error)) {
        LogMessage("Bans: could not save ban list: " + error);
        return false;
    }
    return true;
}

}

void Load() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    try {
        std::vector<BanEntry> loaded;
        if(std::filesystem::exists(Path())) {
            if(std::filesystem::file_size(Path())>8*1024*1024)throw std::runtime_error("oversize");
            std::ifstream file(Path());nlohmann::json content;file>>content;
            if(!content.is_array())throw std::runtime_error("invalid ban list");
            std::set<std::string> ids;
            for(const auto& item:content) {
                BanEntry ban;ban.netId=item.at("netId").get<std::string>();
                ban.name=item.value("name",std::string{});ban.reason=item.value("reason",std::string{});ban.bannedAt=item.value("bannedAt",std::string{});
                if(ban.netId.empty() || ban.netId.size()>128 || !ids.insert(ban.netId).second)throw std::runtime_error("invalid ban identity");
                loaded.push_back(std::move(ban));
            }
        }
        g_Bans=std::move(loaded);g_Loaded=true;
        LogMessage("Bans: loaded "+std::to_string(g_Bans.size())+" records");
    } catch(...) {
        g_Loaded=false;
        LogMessage("Bans: unreadable or invalid ban list; original file preserved and mutations disabled");
    }
}
bool IsReady() {std::lock_guard<std::mutex> lock(g_Mutex);return g_Loaded;}

std::vector<BanEntry> All() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    return g_Bans;
}

bool IsBanned(const std::string& netId) {
    if (netId.empty()) return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const BanEntry& ban : g_Bans) {
        if (ban.netId == netId) return true;
    }
    return false;
}

bool Ban(const std::string& player, const std::string& reason, std::string& outMessage) {
    if(!IsReady()){outMessage="ban store unavailable";return false;}
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) {
        outMessage = "engine not initialised";
        return false;
    }

    std::string netId;
    std::string name;
    std::string wanted = Lower(player);
    size_t matches=0;
    for (const PlayerInfo& p : engine->GetAllPlayers()) {
        if (std::to_string(p.playerId) == player || Lower(p.uniqueNetId) == wanted ||
            Lower(p.characterGuid) == wanted || Lower(p.characterName) == wanted ||
            Lower(p.name) == wanted) {
            netId = p.uniqueNetId;
            name = p.characterName.empty()?p.name:p.characterName;
            ++matches;
        }
    }

    if(matches>1){outMessage="ambiguous player; use a unique network ID, player ID or character GUID";return false;}
    if(matches==1 && netId.empty()){outMessage="matched player has no usable network ID; ban was not added";return false;}
    if (netId.empty()) {
        bool looksLikeSteamId = player.size() == 17 &&
                                player.find_first_not_of("0123456789") == std::string::npos;
        if (!looksLikeSteamId) {
            outMessage = "no connected player matches '" + player +
                         "'; to ban someone offline use their SteamID64";
            return false;
        }
        netId = player;
    }

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const BanEntry& ban : g_Bans) {
            if (ban.netId == netId) {
                outMessage = (name.empty() ? netId : name) + " is already banned";
                return false;
            }
        }
        g_Bans.push_back({netId, name, reason, Now()});
        if (!SaveLocked()) {
            g_Bans.pop_back();
            outMessage = "could not persist ban; ban was not added";
            return false;
        }
    }

    std::string kickError;
    bool online = engine->KickPlayer(netId, reason, kickError);

    outMessage = "banned " + (name.empty() ? netId : name) + " (" + reason + ")" +
                 (online ? "" : ", immediate kick not confirmed: " + kickError);
    LogMessage("Bans: " + outMessage);
    return true;
}

bool Unban(const std::string& identifier, std::string& outMessage) {
    std::string wanted = Lower(identifier);
    std::lock_guard<std::mutex> lock(g_Mutex);
    if(!g_Loaded){outMessage="ban store unavailable";return false;}
    size_t chosen=g_Bans.size(),matches=0;
    for(size_t i=0;i<g_Bans.size();++i)if(Lower(g_Bans[i].netId)==wanted){chosen=i;++matches;}
    if(!matches)for(size_t i=0;i<g_Bans.size();++i)if(!g_Bans[i].name.empty() && Lower(g_Bans[i].name)==wanted){chosen=i;++matches;}
    if(matches>1){outMessage="ambiguous ban name; use the stored network ID";return false;}
    if(matches==1){
        const auto removed=g_Bans[chosen];g_Bans.erase(g_Bans.begin()+(long)chosen);
        if(!SaveLocked()){g_Bans.insert(g_Bans.begin()+(long)chosen,removed);outMessage="could not persist unban; ban remains active";return false;}
        outMessage="unbanned "+(removed.name.empty()?removed.netId:removed.name);LogMessage("Bans: "+outMessage);return true;
    }

    outMessage = "no ban matches '" + identifier + "'";
    return false;
}

void Enforce() {
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized() || !GameThread::IsReady()) return;

    std::vector<BanEntry> bans = All();
    if (bans.empty()) return;

    for (const PlayerInfo& player : engine->GetAllPlayers()) {
        if (player.uniqueNetId.empty()) continue;
        for (const BanEntry& ban : bans) {
            if (ban.netId != player.uniqueNetId) continue;
            LogMessage("Bans: '" + player.name + "' connected while banned, removing");
            std::string error;
            engine->KickPlayer(player.uniqueNetId, ban.reason, error);
            break;
        }
    }
}

}
