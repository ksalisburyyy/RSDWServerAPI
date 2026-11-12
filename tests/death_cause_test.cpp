#include "thc/store.h"
#include "thc/creature_names.h"
#include <cassert>
#include <filesystem>
#include <iostream>
using Thc::Json;
int main(){
 char root[]="/tmp/thc-death-cause-XXXXXX";assert(mkdtemp(root));Thc::Store store(root);assert(store.Load());
 DomEngine::PlayerInfo p;p.characterGuid=std::string(32,'A');p.characterName="Callmebury";p.uniqueNetId="76561190000000000";p.pawnPtr=0x1000;p.hasHealth=true;p.health=p.maxHealth=100;
 store.Tick({p},true,1000,10);
 auto death=[&](double now,const std::string& cause,bool confirmed,bool settled,const std::string& victimId="4096:1"){
  p.isDead=false;p.health=100;store.Tick({p},true,(int64_t)now+990,now-1);
  Thc::Hit hit;hit.victimGuid=p.characterGuid;hit.victimId=victimId;hit.observed=now;hit.cause=cause;hit.fatalConfirmed=confirmed;hit.causeSettled=settled;hit.causeEvidence="confirmed test witness";
  p.health=0;p.isDead=true;auto events=store.Tick({p},true,(int64_t)now+1000,now+0.1,{hit});assert(events.size()==1);return events[0];
 };
 auto event=death(20,"Goblin",true,true);assert(event["details"]["cause"]=="Goblin" && event["details"]["causeSettled"]==true);
 assert(store.Read()["characters"][p.characterGuid]["deathsByCause"]["Goblin"]==1);
 event=death(30,"Fall damage",true,true);assert(event["details"]["cause"]=="Fall damage");
 event=death(40,"Suicide",true,true);assert(event["details"]["cause"]=="Suicide");
 event=death(50,"Goblin",false,true);assert(event["details"]["cause"]=="unattributed");
 event=death(60,"Goblin",true,false);assert(event["details"]["cause"]=="unattributed");
 event=death(70,"Goblin",true,true,"8192:1");assert(event["details"]["cause"]=="unattributed");
 p.isDead=false;p.health=100;store.Tick({p},true,1080,80);
 Thc::Hit stale;stale.victimGuid=p.characterGuid;stale.victimId="4096:1";stale.observed=79;stale.cause="Goblin";stale.fatalConfirmed=stale.causeSettled=true;
 p.isDead=true;p.health=0;auto events=store.Tick({p},true,1090,90,{stale});assert(events.size()==1 && events[0]["details"]["cause"]=="unattributed");
 assert(!Thc::CreatureName("BP_AI_MeleeGoblin_Character_C").empty());
 assert(Thc::CreatureName("BP_UnverifiedGoblinLikeThing_C").empty());
 assert(store.Tick({p},true,1092,92).empty());
 std::filesystem::remove_all(root);
 std::cout<<"PASS: confirmed mob/fall/explicit-suicide settlement, stale/nonfatal/unknown/other-pawn refusal, per-cause ledger, death deduplication and exact creature-name mapping\n";
}
