#pragma once
#include "../vendor/nlohmann_json.h"
#include "../engine/dom_engine.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Thc {
using Json = nlohmann::json;
struct Hit {
    std::string victimGuid, attackerGuid, victimId, victimName, weapon, cause;
    bool npcDeath = false;
    bool fatalConfirmed = false, causeSettled = false, deathEvent = false;
    std::string causeEvidence;
    uint64_t deathSequence = 0; // Assigned only by validated engine death telemetry.
    double observed = 0;
};
class Store {
    struct Session { DomEngine::PlayerInfo player; double last = 0; int misses = 0; bool deathKnown = false, dead = false; bool telemetrySeen = false; double healthDeathAt = -1; uintptr_t healthDeathPawn = 0; bool pendingDeath = false; double pendingSince = 0; int64_t pendingWall = 0; DomEngine::PlayerInfo pendingPlayer; };
    std::string directory;
    mutable std::mutex mutex;
    Json state;
    std::map<std::string, Session> sessions;
    std::map<std::string, Hit> damage;
    std::map<std::string, double> credited;
    std::map<uint64_t, double> creditedDeaths;
    bool writable = false;
    std::string error;
    bool Save(const Json& candidate);
    void Event(Json& target, std::vector<Json>& emitted, const std::string& type,
               const std::string& guid, const std::string& name, int64_t wall, Json details = Json::object());
public:
    explicit Store(std::string directory);
    bool Load();
    std::vector<Json> Tick(const std::vector<DomEngine::PlayerInfo>& players, bool valid,
                           int64_t wall, double monotonic, const std::vector<Hit>& hits = {}, double deathSettlementSeconds = 0,
                           const std::map<std::string,std::string>& deathBindings = {});
    std::vector<Json> Record(const std::string& type, const std::string& guid,
                             const std::string& name, int64_t wall, Json details);
    Json Read() const;
    Json Status() const;
};
}
