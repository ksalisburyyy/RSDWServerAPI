#pragma once
#include "store.h"
#include "creature_names.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <regex>
#include <set>

namespace Thc::LogDamage {
struct Prediction { std::string victim,source; double wall=0,amount=0; };
// Only the engine's complete, timestamped EvadeAttack narration is authority.
// Chat, attack telegraphs, evades and zero damage cannot become damaging hits.
inline bool Parse(const std::string& line,Prediction& out) {
    if(line.size()>4096 || line.find("EvadeAttack: Verbose: [DedicatedServer] PredictPointDamage() : ")==std::string::npos)return false;
    static const std::regex pattern(R"(^\[([0-9]{4}\.[0-9]{2}\.[0-9]{2}-[0-9]{2}\.[0-9]{2}\.[0-9]{2}):([0-9]{3})\]\[[ 0-9]+\]EvadeAttack: Verbose: \[DedicatedServer\] PredictPointDamage\(\) : \[([0-9]+\.[0-9]+), [0-9]+\] (BP_[A-Za-z0-9_]+_C_[0-9]+) will receive ([0-9]+\.[0-9]+) damage at ([0-9]+\.[0-9]+) from (BP_[A-Za-z0-9_]+_C_[0-9]+)\.$)");
    std::smatch m;if(!std::regex_match(line,m,pattern))return false;
    std::tm tm{};auto stamp=m[1].str();char* end=strptime(stamp.c_str(),"%Y.%m.%d-%H.%M.%S",&tm);
    if(!end || *end)return false;
    auto epoch=timegm(&tm);char roundtrip[32]{};gmtime_r(&epoch,&tm);strftime(roundtrip,sizeof(roundtrip),"%Y.%m.%d-%H.%M.%S",&tm);
    if(stamp!=roundtrip)return false;
    try {
        out.wall=(double)epoch+std::stoi(m[2].str())/1000.0;out.amount=std::stod(m[5].str());
        double delay=std::stod(m[6].str())-std::stod(m[3].str());
        if(!std::isfinite(out.amount) || out.amount<=0 || out.amount>1000000 || !std::isfinite(delay) || delay<-.05 || delay>.5)return false;
        out.victim=m[4].str();out.source=m[7].str();return true;
    }catch(...){return false;}
}
inline std::string SourceName(const std::string& actor) {
    auto suffix=actor.rfind("_C_");if(suffix==std::string::npos)return {};
    return CreatureName(actor.substr(0,suffix+2));
}
struct Sample {std::string actor,guid;uintptr_t pawn=0;int index=-1;double health=0;bool dead=false;};
class Witness {
    std::map<std::string,Sample> previous;
    double lastWall=0;
public:
    uint64_t parsed=0,settled=0,rejected=0;
    void Reset(){previous.clear();lastWall=0;}
    std::vector<Hit> Observe(const std::vector<Sample>& samples,const std::vector<std::string>& lines,double wall,double now,bool complete=true) {
        std::map<std::string,Sample> current;std::map<std::string,std::string> identities;std::set<std::string> ambiguous;
        for(const auto& p:samples){
            if(p.actor.empty() || p.guid.empty() || !p.pawn || p.index<0 || !std::isfinite(p.health) || p.health<0 || p.health>1e7 || p.dead!=(p.health==0))continue;
            if(!current.emplace(p.actor,p).second)ambiguous.insert(p.actor);
            auto identity=identities.emplace(p.guid,p.actor);
            if(!identity.second){ambiguous.insert(p.actor);ambiguous.insert(identity.first->second);}
        }
        std::map<std::string,std::vector<Prediction>> candidates;std::set<std::string> uniqueLines;
        for(const auto& line:lines){if(!uniqueLines.insert(line).second)continue;Prediction p;if(Parse(line,p)){++parsed;candidates[p.victim].push_back(std::move(p));}}
        std::vector<Hit> hits;
        if(complete && lastWall>0 && wall>=lastWall && wall-lastWall<=3 && std::isfinite(now))for(const auto& entry:current){
            const auto& p=entry.second;auto old=previous.find(entry.first);
            if(ambiguous.count(entry.first) || old==previous.end() || !p.dead || old->second.dead || old->second.health<=0 || p.guid!=old->second.guid || p.pawn!=old->second.pawn || p.index!=old->second.index)continue;
            auto found=candidates.find(entry.first);if(found==candidates.end())continue;
            std::set<std::string> attackers;std::string cause;double total=0;bool valid=true;
            for(const auto& prediction:found->second){
                // No old session history, backlog or future timestamps.
                if(prediction.wall<lastWall-.05 || prediction.wall>wall+.05 || wall-prediction.wall>3){valid=false;break;}
                auto name=SourceName(prediction.source);if(name.empty()){valid=false;break;}
                attackers.insert(prediction.source);cause=name;total+=prediction.amount;
            }
            // The logged amount must explain the entire observed lethal loss.
            // Two different attackers never settle by choosing whichever logged last.
            if(!valid || attackers.size()!=1 || !std::isfinite(total) || total+.01<old->second.health){++rejected;continue;}
            Hit hit;hit.victimGuid=p.guid;hit.victimId=std::to_string(p.pawn)+":"+std::to_string(p.index);
            hit.cause=cause;hit.fatalConfirmed=hit.causeSettled=false;hit.observed=now;
            hit.causeEvidence="candidate attacker from current-session combat prediction; landed fatal cause not confirmed";
            hits.push_back(std::move(hit));++settled;
        }
        previous=std::move(current);for(const auto& key:ambiguous)previous.erase(key);lastWall=wall;return hits;
    }
};
}
