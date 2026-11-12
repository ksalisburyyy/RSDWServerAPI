#include "thc/store.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
int main(){
 char directory[]="/tmp/thc-event-ledger-XXXXXX";assert(mkdtemp(directory));Thc::Store store(directory);assert(store.Load());
 DomEngine::PlayerInfo p;p.characterGuid=std::string(32,'A');p.characterName="Callmebury";p.pawnPtr=0x1000;p.hasHealth=false;p.hasLocation=true;p.x=1;p.y=2;p.z=3;
 std::map<std::string,std::string> bindings={{p.characterGuid,"4096:1"}};
 store.Tick({p},true,1000,10,{},3,bindings);
 auto event=[&](uint64_t sequence,double now,const std::string& cause="Runt Goblin"){
  Thc::Hit hit;hit.deathEvent=true;hit.deathSequence=sequence;hit.fatalConfirmed=true;hit.victimGuid=p.characterGuid;hit.victimId="4096:1";hit.observed=now;hit.cause=cause;hit.causeSettled=!cause.empty();hit.causeEvidence="validated engine test event";return hit;
 };
 // Confirmed engine deaths work with unavailable health and no sampled health edge.
 auto first=event(1,11);auto rows=store.Tick({p},true,1001,11,{first},3,bindings);
 assert(rows.size()==1 && rows[0]["type"]=="death" && rows[0]["details"]["cause"]=="Runt Goblin");
 assert(rows[0]["details"]["source"]=="validated engine death event");
 assert(store.Tick({p},true,1002,12,{first,first},3,bindings).empty());
 // A later polling transition must not duplicate the already recorded event.
 p.hasHealth=true;p.health=0;p.isDead=true;assert(store.Tick({p},true,1003,13,{},3,bindings).empty());
 p.health=100;p.isDead=false;assert(store.Tick({p},true,1004,14,{},3,bindings).empty());
 // Fast deaths/respawns between polls: engine events count even if HP stays positive.
 auto second=event(2,15);rows=store.Tick({p},true,1005,15,{second},3,bindings);assert(rows.size()==1);
 rows=store.Tick({p},true,1006,16,{event(4,16),event(3,15.5)},3,bindings);assert(rows.size()==2);
 assert(store.Read()["characters"][p.characterGuid]["deaths"]==4);
 // Neither predictions nor stale/future/nonfinite events establish a death.
 auto prediction=event(5,17);prediction.deathEvent=false;
 assert(store.Tick({p},true,1007,17,{prediction,event(6,10),event(7,18),event(8,std::numeric_limits<double>::quiet_NaN())},3,bindings).empty());
 auto unbound=event(9,18);unbound.victimGuid=std::string(32,'B');assert(store.Tick({p},true,1008,18,{unbound},3,bindings).empty());
 // Real death with unresolved cause is still a death; no fabricated attribution.
 rows=store.Tick({p},true,1009,19,{event(10,19,"")},3,bindings);assert(rows.size()==1 && rows[0]["details"]["cause"]=="unattributed");
 // Respawn may replace the pawn before the event is drained. Captured identity
 // remains valid; do not attach the new pawn's location to the old death.
 p.pawnPtr=0x2000;bindings[p.characterGuid]="8192:2";
 rows=store.Tick({p},true,1010,20,{event(11,20,"Fall damage")},3,bindings);
 assert(rows.size()==1 && rows[0]["details"]["cause"]=="Fall damage" && !rows[0]["details"].contains("location"));
 assert(store.Read()["characters"][p.characterGuid]["deaths"]==6);
 assert(store.Read()["characters"][p.characterGuid]["deathsByCause"]["Runt Goblin"]==4);
 // Active telemetry bindings do not fabricate deaths from sampled health alone.
 p.health=0;p.isDead=true;assert(store.Tick({p},true,1011,21,{},3,bindings).empty());
 std::filesystem::remove_all(directory);
 std::cout<<"PASS: engine deaths without health edge, quick respawns, duplicate prevention, multiple queued deaths, stale/future/nonfinite/prediction refusal, unknown causes and old-pawn location isolation\n";
}
