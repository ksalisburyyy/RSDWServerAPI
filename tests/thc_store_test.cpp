#include "thc/store.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using Thc::Json;
int main(){
 char base[]="/tmp/thc-store-XXXXXX";assert(mkdtemp(base));std::string directory=std::string(base)+"/state";
 Thc::Store store(directory);assert(store.Load());
 DomEngine::PlayerInfo p;p.characterGuid="AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";p.characterName="First name";p.uniqueNetId="76561190000000000";p.hasHealth=true;p.maxHealth=100;p.health=100;p.playerStatePtr=0x123456;
 auto events=store.Tick({p},true,1000,10);assert(events.size()==1 && events[0]["type"]=="join");
 p.characterName="Renamed character";events=store.Tick({p},true,1002,12);assert(events.empty());
 auto data=store.Read();assert(data["characters"][p.characterGuid]["joins"]==1);assert(data["characters"][p.characterGuid]["name"]==p.characterName);
 auto missing=p;missing.characterGuid="";missing.characterName="";
 assert(store.Tick({missing},true,1004,14).empty());assert(store.Tick({missing},true,1006,16).empty());assert(store.Read()["onlineGuids"].size()==1);
 Thc::Hit damage;damage.victimGuid=p.characterGuid;damage.cause="BP_Wolf_C";damage.observed=17;
 p.health=0;p.isDead=true;events=store.Tick({p},true,1008,18,{damage});assert(events.size()==1 && events[0]["type"]=="death");assert(events[0]["details"]["cause"]=="unattributed");assert(events[0]["details"]["lastObservedCauser"]=="BP_Wolf_C");
 assert(store.Tick({p},true,1010,20).empty());p.isDead=false;p.health=100;store.Tick({p},true,1012,22);p.isDead=true;p.health=0;
 events=store.Tick({p},true,1014,24);assert(events.size()==1 && events[0]["type"]=="death");assert(store.Read()["characters"][p.characterGuid]["deaths"]==2);
 Thc::Hit kill;kill.attackerGuid=p.characterGuid;kill.victimId="100:123";kill.victimName="Goblin";kill.weapon="Sword";kill.npcDeath=true;kill.observed=25;
 events=store.Tick({p},true,1016,26,{kill});assert(events.size()==1 && events[0]["type"]=="kill");assert(store.Tick({p},true,1018,28,{kill}).empty());
 kill.attackerGuid="";kill.victimId="100:124";kill.observed=29;assert(store.Tick({p},true,1020,30,{kill}).empty());assert(store.Read()["characters"][p.characterGuid]["kills"]==1);
 assert(store.Tick({},false,1022,32).empty());assert(store.Tick({},false,1024,34).empty());assert(store.Read()["onlineGuids"].size()==1);
 assert(store.Tick({},true,1026,36).empty());events=store.Tick({},true,1028,38);assert(events.size()==1 && events[0]["type"]=="leave");
 events=store.Tick({p},true,1030,40);assert(events.size()==1 && events[0]["type"]=="join");assert(store.Read()["characters"][p.characterGuid]["joins"]==2);
 // Save failure rolls back session changes as well as counters.
 std::filesystem::rename(directory,directory+"-moved");auto q=p;q.characterGuid="BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";q.characterName="Another character";
 assert(store.Tick({p,q},true,1032,42).empty());assert(!store.Read()["characters"].contains(q.characterGuid));
 std::filesystem::rename(directory+"-moved",directory);events=store.Tick({p,q},true,1034,44);assert(events.size()==1);assert(store.Read()["characters"][q.characterGuid]["joins"]==1);
 Thc::Store loaded(directory);assert(loaded.Load());assert(loaded.Read()["characters"][p.characterGuid]["kills"]==1);
 assert(loaded.Read()["characters"][p.characterGuid]["deaths"]==2);
 std::ofstream(directory+"/state.json")<<"broken";Thc::Store corrupt(directory);assert(!corrupt.Load());assert(corrupt.Record("test","","",0,Json::object()).empty());
 std::ifstream file(directory+"/state.json");std::string preserved;file>>preserved;assert(preserved=="broken");
 std::filesystem::remove_all(base);
 std::cout<<"PASS: GUID identity, rename, transient identity loss, death/kill deduplication, unknown attribution, leave debounce, invalid snapshots, reconnect, persistence and failure rollback\n";
}
