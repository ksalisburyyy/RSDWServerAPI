#include "store.h"
#include "../utils/atomic_file.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace Thc {
namespace {
Json Empty() { return {{"schema",1},{"nextEvent",1},{"characters",Json::object()},{"events",Json::array()}}; }
bool Guid(const std::string& value) {
    return value.size()==32 && value.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos && value!=std::string(32,'0');
}
std::string Key(std::string value) { std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return (char)toupper(c);}); return value; }
Json Character(const DomEngine::PlayerInfo& p, int64_t now) {
    return {{"guid",Key(p.characterGuid)},{"netId",p.uniqueNetId},{"name",p.characterName},
            {"firstSeen",now},{"lastSeen",now},{"joins",0},{"secondsPlayed",0.0},
            {"kills",0},{"deaths",0},{"killsByMob",Json::object()},
            {"killsByWeapon",Json::object()},{"deathsByCause",Json::object()}};
}
void Increment(Json& object, const std::string& key) { object[key]=object.value(key,uint64_t{0})+1; }
}
Store::Store(std::string path):directory(std::move(path)),state(Empty()){}
bool Store::Load() {
    std::lock_guard<std::mutex> lock(mutex);
    sessions.clear(); damage.clear(); credited.clear(); creditedDeaths.clear();
    try {
        std::filesystem::create_directories(directory);
        std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
        const auto path=directory+"/state.json";
        if(std::filesystem::exists(path)) {
            if(std::filesystem::file_size(path)>32*1024*1024) throw std::runtime_error("state exceeds size limit");
            std::ifstream file(path); Json loaded; file>>loaded;
            if(loaded.at("schema")!=1 || !loaded.at("characters").is_object() || !loaded.at("events").is_array() ||
               loaded.at("events").size()>1000 || !loaded.at("nextEvent").is_number_unsigned())
                throw std::runtime_error("unsupported state format");
            for(auto it=loaded["characters"].begin();it!=loaded["characters"].end();++it) {
                if(!Guid(it.key()) || !it->at("name").is_string() || !it->at("netId").is_string()) throw std::runtime_error("invalid character");
                for(auto key:{"joins","kills","deaths"}) if(!it->at(key).is_number_unsigned()) throw std::runtime_error("invalid counter");
                if(!it->at("secondsPlayed").is_number() || it->at("secondsPlayed").get<double>()<0) throw std::runtime_error("invalid playtime");
                for(auto key:{"killsByMob","killsByWeapon","deathsByCause"}) {
                    if(!it->at(key).is_object()) throw std::runtime_error("invalid breakdown");
                    for(auto& count:it->at(key)) if(!count.is_number_unsigned()) throw std::runtime_error("invalid breakdown count");
                }
            }
            uint64_t last=0;
            for(const auto& event:loaded["events"]) {
                if(!event.at("id").is_number_unsigned() || event.at("id").get<uint64_t>()<=last || !event.at("type").is_string() ||
                   !event.at("name").is_string() || !event.at("guid").is_string() || !event.at("time").is_number_integer() || !event.at("details").is_object())
                    throw std::runtime_error("invalid event history");
                last=event.at("id").get<uint64_t>();
            }
            if(loaded["nextEvent"].get<uint64_t>()<=last)throw std::runtime_error("invalid event sequence");
            state=std::move(loaded);
        } else state=Empty();
        writable=true; error.clear(); return true;
    } catch(const std::exception&) {
        // Preserve the original file for recovery. Never replace an unreadable ledger.
        writable=false; error="ledger unreadable or invalid; original state preserved"; return false;
    }
}
bool Store::Save(const Json& candidate) {
    if(!writable) return false;
    std::string reason;
    try {
        if(!WriteFileAtomically(directory+"/state.json",candidate.dump(),reason)) {error="ledger save failed";return false;}
    }catch(...){error="ledger serialization failed; original preserved";return false;}
    error.clear(); return true;
}
void Store::Event(Json& target,std::vector<Json>& emitted,const std::string& type,
                  const std::string& guid,const std::string& name,int64_t wall,Json details) {
    Json ev={{"id",target["nextEvent"]},{"type",type},{"guid",guid},{"name",name},{"time",wall},{"details",std::move(details)}};
    target["nextEvent"]=target["nextEvent"].get<uint64_t>()+1;
    target["events"].push_back(ev); emitted.push_back(std::move(ev));
    if(target["events"].size()>1000) target["events"].erase(target["events"].begin());
}
std::vector<Json> Store::Tick(const std::vector<DomEngine::PlayerInfo>& players,bool valid,
                            int64_t wall,double now,const std::vector<Hit>& hits,double deathSettlementSeconds,
                            const std::map<std::string,std::string>& deathBindings) {
    std::lock_guard<std::mutex> lock(mutex);
    if(!writable || !valid || !std::isfinite(now)) return {};
    auto next=state; auto online=sessions; auto recent=damage; auto deadNpcs=credited; auto deadPlayers=creditedDeaths;
    std::set<std::string> incomingDeaths;
    for(const auto& hit:hits)if(hit.deathEvent && hit.fatalConfirmed && hit.deathSequence && Guid(hit.victimGuid) && std::isfinite(hit.observed) && now>=hit.observed && now-hit.observed<=5)incomingDeaths.insert(Key(hit.victimGuid));
    std::vector<Json> emitted; std::set<std::string> seen; bool dirty=false;
    for(const auto& hit:hits) {
        if(!std::isfinite(hit.observed) || now-hit.observed<0 || now-hit.observed>10) continue;
        if(Guid(hit.victimGuid)) {
            auto key=Key(hit.victimGuid);auto previous=recent.find(key);
            if(previous==recent.end() || hit.fatalConfirmed || !previous->second.fatalConfirmed)recent[key]=hit;
        }
        auto attacker=Key(hit.attackerGuid);
        if(!hit.npcDeath || hit.victimId.empty() || !Guid(attacker) || !online.count(attacker) || !next["characters"].contains(attacker)) continue;
        if(deadNpcs.count(hit.victimId)) continue;
        deadNpcs[hit.victimId]=now;
        auto& row=next["characters"][attacker]; Increment(row,"kills");
        Increment(row["killsByMob"],hit.victimName.empty()?"unknown":hit.victimName);
        Increment(row["killsByWeapon"],hit.weapon.empty()?"unresolved":hit.weapon);
        Event(next,emitted,"kill",attacker,row["name"],wall,{{"victim",hit.victimName},{"weapon",hit.weapon},
              {"evidence","positive observed damage and zero victim health"},{"attribution","last observed damaging call"}});
        dirty=true;
    }
    auto deathDetails=[&](const DomEngine::PlayerInfo& p){
        auto guid=Key(p.characterGuid);Json details={{"source","observed health transition"}};std::string cause="unattributed";
        auto hit=recent.find(guid);
        if(hit!=recent.end() && now>=hit->second.observed && now-hit->second.observed<=10){
            const auto& evidence=hit->second;details["lastObservedCauser"]=evidence.cause;
            details["attackerGuid"]=evidence.attackerGuid;details["weapon"]=evidence.weapon;
            details["evidence"]="recent damage candidate; final cause not settled";
            bool pawnMatches=p.pawnPtr && evidence.victimId.rfind(std::to_string(p.pawnPtr)+":",0)==0;
            if(pawnMatches && evidence.fatalConfirmed && evidence.causeSettled && !evidence.cause.empty() && now-evidence.observed<=(evidence.deathEvent?5.0:2.5)){cause=evidence.cause;details["evidence"]=evidence.causeEvidence;details["causeSettled"]=true;}
        }
        details["cause"]=cause;
        if(p.hasLocation)details["location"]={{"x",p.x},{"y",p.y},{"z",p.z}};
        return details;
    };
    auto emitDeath=[&](const DomEngine::PlayerInfo& p,int64_t deathWall){
        auto guid=Key(p.characterGuid);auto& row=next["characters"][guid];auto details=deathDetails(p);
        auto session=online.find(guid);if(session!=online.end()){session->second.healthDeathAt=now;session->second.healthDeathPawn=p.pawnPtr;}
        Increment(row,"deaths");Increment(row["deathsByCause"],details["cause"].get<std::string>());
        Event(next,emitted,"death",guid,p.characterName,deathWall,std::move(details));
    };
    for(const auto& p:players) {
        if(!Guid(p.characterGuid) || p.characterName.empty()) {
            // Keep existing sessions while an identity field is temporarily unreadable.
            for(const auto& previous:online)if((!p.uniqueNetId.empty() && p.uniqueNetId==previous.second.player.uniqueNetId) ||
                (p.playerStatePtr && p.playerStatePtr==previous.second.player.playerStatePtr) ||
                (Guid(p.characterGuid) && Key(p.characterGuid)==previous.first)) seen.insert(previous.first);
            continue;
        }
        const auto guid=Key(p.characterGuid);
        if(!seen.insert(guid).second) continue;
        if(!next["characters"].contains(guid)) next["characters"][guid]=Character(p,wall);
        auto& row=next["characters"][guid]; row["name"]=p.characterName; row["netId"]=p.uniqueNetId; row["lastSeen"]=wall;
        auto it=online.find(guid);
        if(it==online.end()) {
            Session session; session.player=p; session.last=now; session.deathKnown=p.hasHealth; session.dead=p.isDead;
            online[guid]=session; Increment(row,"joins");
            Event(next,emitted,"join",guid,p.characterName,wall,{{"source","observed player snapshot"}});
        } else {
            auto& session=it->second;
            bool engineDeaths=incomingDeaths.count(guid);
            auto binding=deathBindings.find(guid);
            if(binding!=deathBindings.end() && p.pawnPtr && binding->second.rfind(std::to_string(p.pawnPtr)+":",0)==0)engineDeaths=true;
            if(!engineDeaths && session.pendingDeath && (deathDetails(session.pendingPlayer).value("causeSettled",false) || now-session.pendingSince>=std::clamp(deathSettlementSeconds,0.0,5.0) || (p.hasHealth && !p.isDead) || (p.pawnPtr && p.pawnPtr!=session.pendingPlayer.pawnPtr))){
                emitDeath(session.pendingPlayer,session.pendingWall);session.pendingDeath=false;
            }
            if((session.dead && p.hasHealth && !p.isDead) || (session.player.pawnPtr && p.pawnPtr && session.player.pawnPtr!=p.pawnPtr))recent.erase(guid);
            // Only account elapsed time while the previous/current observations are close.
            double seconds=std::clamp(now-session.last,0.0,10.0);
            row["secondsPlayed"]=row["secondsPlayed"].get<double>()+seconds;
            if(!engineDeaths && p.hasHealth && session.deathKnown && !session.dead && p.isDead) {
                if(deathSettlementSeconds>0 && !deathDetails(p).value("causeSettled",false)){
                    session.pendingDeath=true;session.pendingSince=now;session.pendingWall=wall;session.pendingPlayer=p;
                }else emitDeath(p,wall);
            }
            if(p.hasHealth) {session.deathKnown=true; session.dead=p.isDead;}
            session.last=now; session.misses=0; session.player=p;
        }
        dirty=true;
    }
    // Genuine death telemetry is the death witness, not a damage prediction.
    // Requiring a second polling health edge loses rapid respawns/unreadable HP.
    std::vector<const Hit*> deaths;
    for(const auto& hit:hits)if(hit.deathEvent && hit.fatalConfirmed && hit.deathSequence && Guid(hit.victimGuid) &&
        !hit.victimId.empty() && std::isfinite(hit.observed) && now>=hit.observed && now-hit.observed<=5)deaths.push_back(&hit);
    std::sort(deaths.begin(),deaths.end(),[](const Hit* a,const Hit* b){return a->deathSequence<b->deathSequence;});
    for(const auto* evidence:deaths){
        const auto guid=Key(evidence->victimGuid);auto sessionIt=online.find(guid);
        if(sessionIt==online.end() || deadPlayers.count(evidence->deathSequence))continue;
        auto& session=sessionIt->second;deadPlayers[evidence->deathSequence]=now;dirty=true;
        // During the transition from health-only monitoring, correlate a death
        // already emitted for the same pawn instead of posting it twice.
        bool priorHealth=!session.telemetrySeen && session.healthDeathAt>=0 && now-session.healthDeathAt<=5 &&
            session.healthDeathPawn && evidence->victimId.rfind(std::to_string(session.healthDeathPawn)+":",0)==0;
        session.telemetrySeen=true;
        if(priorHealth){session.pendingDeath=false;continue;}
        const auto cause=evidence->causeSettled && !evidence->cause.empty()?evidence->cause:std::string("unattributed");
        Json details={{"source","validated engine death event"},{"cause",cause},{"causeSettled",cause!="unattributed"},
            {"evidence",evidence->causeEvidence},{"lastObservedCauser",evidence->cause},{"weapon",evidence->weapon},
            {"attackerGuid",evidence->attackerGuid},{"deathSequence",evidence->deathSequence}};
        const auto& player=session.pendingDeath?session.pendingPlayer:session.player;
        if(player.hasLocation && player.pawnPtr && evidence->victimId.rfind(std::to_string(player.pawnPtr)+":",0)==0)
            details["location"]={{"x",player.x},{"y",player.y},{"z",player.z}};
        auto& row=next["characters"][guid];Increment(row,"deaths");Increment(row["deathsByCause"],cause);
        Event(next,emitted,"death",guid,row["name"],session.pendingDeath?session.pendingWall:wall,std::move(details));
        session.pendingDeath=false;recent.erase(guid);
    }
    for(auto it=online.begin();it!=online.end();) {
        if(seen.count(it->first)) {++it;continue;}
        if(++it->second.misses<2) {++it;continue;}
        if(it->second.pendingDeath)emitDeath(it->second.pendingPlayer,it->second.pendingWall);
        Event(next,emitted,"leave",it->first,it->second.player.characterName,wall,{{"source","two valid missing snapshots"}});
        recent.erase(it->first);
        it=online.erase(it); dirty=true;
    }
    for(auto it=recent.begin();it!=recent.end();) if(now-it->second.observed>10) it=recent.erase(it); else ++it;
    for(auto it=deadNpcs.begin();it!=deadNpcs.end();) if(now-it->second>120) it=deadNpcs.erase(it); else ++it;
    for(auto it=deadPlayers.begin();it!=deadPlayers.end();)if(now-it->second>10)it=deadPlayers.erase(it);else ++it;
    if(dirty && !Save(next)) return {};
    state=std::move(next); sessions=std::move(online); damage=std::move(recent); credited=std::move(deadNpcs); creditedDeaths=std::move(deadPlayers);
    return emitted;
}
std::vector<Json> Store::Record(const std::string& type,const std::string& guid,
                              const std::string& name,int64_t wall,Json details) {
    std::lock_guard<std::mutex> lock(mutex); if(!writable) return {};
    auto next=state; std::vector<Json> emitted; Event(next,emitted,type,guid,name,wall,std::move(details));
    if(!Save(next)) return {};
    state=std::move(next); return emitted;
}
Json Store::Read() const {
    std::lock_guard<std::mutex> lock(mutex); auto result=state;
    result["onlineGuids"]=Json::array(); for(const auto& session:sessions) result["onlineGuids"].push_back(session.first);
    return result;
}
Json Store::Status() const { std::lock_guard<std::mutex> lock(mutex); return {{"loaded",writable},{"error",error},{"characters",state["characters"].size()},{"online",sessions.size()}}; }
}
