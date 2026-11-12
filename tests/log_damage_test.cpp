#include "thc/log_damage.h"
#include "thc/game_log_tail.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Thc;
int main(){
 std::ifstream file("tests/fixtures/death-combat.log");assert(file);std::vector<std::string> fixture;std::string line;
 while(std::getline(file,line))fixture.push_back(line);
 assert(fixture.size()==19);
 int count=0;LogDamage::Prediction last;
 for(const auto& text:fixture){LogDamage::Prediction p;if(LogDamage::Parse(text,p)){++count;last=p;}}
 assert(count==12);assert(last.amount==24 && LogDamage::SourceName(last.source)=="Runt Goblin");
 assert(!LogDamage::Parse("[2026.10.26-08.44.40:557][859]LogChat: "+fixture.back(),last));
 assert(!LogDamage::Parse(std::string(5000,'a'),last));
 auto actor=last.victim;LogDamage::Sample alive{actor,std::string(32,'A'),0x1000,42,20,false};auto dead=alive;dead.health=0;dead.dead=true;
 const double wall=last.wall+.3;
 auto run=[&](const std::vector<std::string>& lines,LogDamage::Sample before,LogDamage::Sample after,double interval=2,bool complete=true){
  LogDamage::Witness w;assert(w.Observe({before},{},wall-interval,10).empty());return w.Observe({after},lines,wall,12,complete);
 };
 auto hits=run({fixture.back()},alive,dead);assert(hits.size()==1 && hits[0].cause=="Runt Goblin" && !hits[0].causeSettled && !hits[0].fatalConfirmed);
 assert(run({fixture.back()},alive,alive).empty());
 auto higher=alive;higher.health=30;assert(run({fixture.back()},higher,dead).empty());
 assert(run({fixture.back(),fixture.back()},higher,dead).empty());
 assert(run({fixture.back()},alive,dead,5).empty());assert(run({fixture.front()},alive,dead).empty());
 assert(run({fixture.back()},alive,dead,2,false).empty());
 auto changed=dead;changed.guid=std::string(32,'B');assert(run({fixture.back()},alive,changed).empty());
 changed=dead;changed.pawn=0x2000;assert(run({fixture.back()},alive,changed).empty());
 changed=dead;changed.index=43;assert(run({fixture.back()},alive,changed).empty());
 changed=dead;changed.actor+="0";assert(run({fixture.back()},alive,changed).empty());
 auto unknown=fixture.back();auto pos=unknown.find("BP_AI_RuntGoblin_Character_C_");unknown.replace(pos,std::string("BP_AI_RuntGoblin_Character").size(),"BP_UnmappedCreature");
 assert(run({unknown},alive,dead).empty());assert(run({fixture.back(),unknown},alive,dead).empty());
 auto second=fixture.back();second.replace(second.rfind("2147470268"),10,"2147470269");assert(run({fixture.back(),second},alive,dead).empty());
 LogDamage::Witness w;w.Observe({alive},{},wall-2,10);assert(w.Observe({dead},{fixture.back()},wall,12).size()==1);assert(w.Observe({dead},{fixture.back()},wall+.1,12.1).empty());
 w.Reset();assert(w.Observe({dead},{fixture.back()},wall+.2,12.2).empty());
 assert(run({},alive,dead).empty()); // unlogged falls remain unresolved
 // Duplicate character identity cannot bind a death to whichever pawn is convenient.
 LogDamage::Witness ambiguous;auto duplicate=alive;duplicate.actor+="1";duplicate.pawn=0x2000;
 ambiguous.Observe({alive,duplicate},{},wall-2,10);assert(ambiguous.Observe({dead},{fixture.back()},wall,12).empty());
 auto future=fixture.back();future.replace(future.find("08.44.40"),8,"08.44.45");assert(run({future},alive,dead).empty());
 char root[]="/tmp/thc-log-damage-XXXXXX";assert(mkdtemp(root));Store store(root);assert(store.Load());
 DomEngine::PlayerInfo p;p.characterGuid=alive.guid;p.characterName="Callmebury";p.pawnPtr=alive.pawn;p.hasHealth=true;p.health=20;
 store.Tick({p},true,1000,10);p.health=0;p.isDead=true;
 auto events=store.Tick({p},true,1002,12.1,hits);assert(events.size()==1 && events[0]["details"]["cause"]=="unattributed");
 assert(store.Read()["characters"][p.characterGuid]["deathsByCause"]["unattributed"]==1);assert(store.Tick({p},true,1003,13,hits).empty());
 auto path=std::filesystem::path(root)/"game.log";
 {std::ofstream f(path);f<<"historical\n";}GameLogTail tail(path.string());assert(tail.Read().empty() && tail.available);
 {std::ofstream f(path,std::ios::app);f<<"part";}assert(tail.Read().empty() && !tail.complete);
 {std::ofstream f(path,std::ios::app);f<<"ial\n";}assert(tail.Read()==std::vector<std::string>{"partial"} && tail.complete);
 auto generation=tail.generation;std::filesystem::rename(path,path.string()+".old");{std::ofstream f(path);f<<"rotated\n";}
 assert(tail.Read()==std::vector<std::string>{"rotated"} && tail.generation>generation);
 generation=tail.generation;{std::ofstream f(path);f<<"short\n";}assert(tail.Read()==std::vector<std::string>{"short"} && tail.generation>generation);
 generation=tail.generation;{std::ofstream f(path);f<<"other\n";}
 assert(tail.Read()==std::vector<std::string>{"other"} && tail.generation>generation);
 {std::ofstream f(path,std::ios::app);f<<std::string(9000,'x')<<"\nsafe\n";}assert(tail.Read()==std::vector<std::string>{"safe"});
 std::filesystem::create_symlink(path,std::filesystem::path(root)/"link");GameLogTail link((std::filesystem::path(root)/"link").string());assert(link.Read().empty() && !link.available);
 auto fifo=std::filesystem::path(root)/"fifo";assert(mkfifo(fifo.c_str(),0600)==0);GameLogTail pipe(fifo.string());assert(pipe.Read().empty() && !pipe.available);
 std::filesystem::remove_all(root);
 std::cout<<"PASS: owner log fixture, pawn binding, prediction-only attribution refusal, negative cases, ledger deduplication and bounded nonblocking rotation-safe log tail\n";
}
