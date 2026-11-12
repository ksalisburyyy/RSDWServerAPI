#include "thc/death_telemetry.cpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sys/mman.h>
namespace {
std::vector<std::pair<uintptr_t,size_t>> regions;
void Register(void* p,size_t n){regions.push_back({(uintptr_t)p,n});}
std::vector<std::string> names={"None"};
uint32_t Name(const std::string& name){for(size_t i=0;i<names.size();++i)if(names[i]==name)return i;names.push_back(name);return names.size()-1;}
alignas(uintptr_t) unsigned char fn[0xE0]{},param[0xC0]{},script[0x60]{},fields[4][0xC0]{},kinds[4][0x30]{},pawn[0x100]{},goblin[0x400]{},fall[0x100]{},controller[0x400]{},event[64]{};
uintptr_t table=0;int forwarded=0;bool throwCapture=false;
std::vector<DomEngine::PlayerInfo> players;
void Original(void*,void*,void*){++forwarded;}
std::string Permissions(uintptr_t address){
 FILE* maps=fopen("/proc/self/maps","r");assert(maps);char line[1024],permissions[5]{};unsigned long long a,b;
 while(fgets(line,sizeof(line),maps))if(sscanf(line,"%llx-%llx %4s",&a,&b,permissions)==3 && address>=a && address<b){fclose(maps);return permissions;}
 fclose(maps);return {};
}
void Field(unsigned char* f,const char* name,const char* kind,int offset,int size,unsigned char* next=nullptr){
 *(uint32_t*)(f+0x28)=Name(name);*(int32_t*)(f+0x4C)=offset;*(int32_t*)(f+0x3C)=size;*(int32_t*)(f+0x38)=1;*(uintptr_t*)(f+0x20)=(uintptr_t)next;
 int k=std::string(kind)=="FloatProperty"?0:std::string(kind)=="ObjectProperty"?1:std::string(kind)=="IntProperty"?2:3;
 *(uint32_t*)kinds[k]=Name(kind);*(uintptr_t*)(f+8)=(uintptr_t)kinds[k];
}
}
namespace Mem {
bool Readable(const void* pointer,size_t size){auto p=(uintptr_t)pointer;for(auto r:regions)if(p>=r.first && p-r.first<=r.second && size<=r.second-(p-r.first))return true;return false;}
bool InImage(const void* p){return p==(void*)table || p==(void*)&Original;}
}
namespace GameThread {
bool IsReady(){return true;}bool HasProcessEvent(){return true;}double PumpAgeSeconds(){return .1;}int VTableIndex(){return 77;}
bool RunSync(const std::function<void()>& fn,int){fn();return true;}
}
namespace DomEngine {
Engine* g_Engine=nullptr;
std::string Engine::NameToString(uint32_t id,uint32_t)const{return id<names.size()?names[id]:"";}
uintptr_t Engine::FindFunction(const char* owner,const char* name)const{return std::string(owner)=="DominionPlayerCharacter" && std::string(name)=="Client_SendDeathEventTelemetry"?(uintptr_t)fn:0;}
bool Engine::IsA(uintptr_t object,const char* name)const{std::string type=name;return (object==(uintptr_t)script && type=="ScriptStruct") || (object==(uintptr_t)pawn && type=="DominionPlayerCharacter") || (object==(uintptr_t)goblin && type=="DominionAICharacter") || (object==(uintptr_t)controller && type=="Controller");}
std::string Engine::GetObjectName(uintptr_t object)const{return object==(uintptr_t)script?"DominionDamageEvent":object==(uintptr_t)fn?"Client_SendDeathEventTelemetry":"";}
std::string Engine::GetObjectClassName(uintptr_t object)const{if(throwCapture)throw std::runtime_error("capture failure");return object==(uintptr_t)fall?"BP_FallDamageActor_C":object==(uintptr_t)goblin?"BP_AI_RuntGoblin_Character_C":"";}
std::vector<PlayerInfo> Engine::GetAllPlayers()const{return players;}
}
int main(){
 auto page=(size_t)sysconf(_SC_PAGESIZE);auto memory=mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory!=MAP_FAILED);table=(uintptr_t)memory;Register(memory,page);
 assert(mprotect((char*)memory+page,page,PROT_NONE)==0);*(uintptr_t*)(table+77*8)=(uintptr_t)&Original;*(uintptr_t*)pawn=table;assert(mprotect(memory,page,PROT_READ)==0);
 for(auto r:{std::pair<void*,size_t>{fn,sizeof(fn)},{param,sizeof(param)},{script,sizeof(script)},{fields,sizeof(fields)},{kinds,sizeof(kinds)},{pawn,sizeof(pawn)},{goblin,sizeof(goblin)},{fall,sizeof(fall)},{controller,sizeof(controller)},{event,sizeof(event)}})Register(r.first,r.second);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;
 Thc::DamageFields::Layout layout{0x28,0x20,0x4C,0x3C,8,0,true};
 Field(param,"DamageEvent","StructProperty",0,64);*(uintptr_t*)(param+0x78)=(uintptr_t)script;
 Field(fields[0],"Amount","FloatProperty",0,4,fields[1]);Field(fields[1],"Instigator","ObjectProperty",8,8,fields[2]);Field(fields[2],"Source","ObjectProperty",16,8,fields[3]);Field(fields[3],"ID","IntProperty",24,4);
 *(uintptr_t*)(script+0x50)=(uintptr_t)fields;*(uintptr_t*)(fn+0x50)=(uintptr_t)param;*(uint32_t*)(fn+0xB4)=(64u<<16)|1;*(uint32_t*)(fn+0xB0)=0x01000440;
 for(auto object:{pawn,goblin,fall})*(int32_t*)(object+DomEngine::Offsets::UObject_Index)=42;
 DomEngine::PlayerInfo p;p.pawnPtr=(uintptr_t)pawn;p.characterGuid=std::string(32,'A');p.characterName="Callmebury";p.hasHealth=true;p.health=100;players={p};
 uintptr_t function;uint32_t shape;int offset;Thc::ReflectedDamage::EventLayout eventLayout;std::string reason;
 assert(Thc::DeathTelemetry::Discover(layout,function,shape,offset,eventLayout,reason));
 // An actual death RPC does not need a persistent event ID to prove death.
 *(uintptr_t*)(fields[2]+0x20)=0;assert(Thc::DeathTelemetry::Discover(layout,function,shape,offset,eventLayout,reason) && eventLayout.id==-1);
 *(uintptr_t*)(fields[2]+0x20)=(uintptr_t)fields[3];
 *(uintptr_t*)(param+0x80)=(uintptr_t)script;assert(!Thc::DeathTelemetry::Discover(layout,function,shape,offset,eventLayout,reason));*(uintptr_t*)(param+0x80)=0;
 *(uint32_t*)(fn+0xB0)=0x400;assert(!Thc::DeathTelemetry::Discover(layout,function,shape,offset,eventLayout,reason));*(uint32_t*)(fn+0xB0)=0x01000440;
 *(int32_t*)(fields[2]+0x4C)=60;assert(!Thc::DeathTelemetry::Discover(layout,function,shape,offset,eventLayout,reason));*(int32_t*)(fields[2]+0x4C)=16;
 Thc::DeathTelemetry::Update(players,layout);assert(Thc::DeathTelemetry::Status()["observedPlayerVtables"]==1);
 assert(Thc::DeathTelemetry::Bindings().at(p.characterGuid)==std::to_string(p.pawnPtr)+":42");
 auto hook=(void(*)(void*,void*,void*))Mem::ReadPtr(table+77*8);assert(hook!=&Original);
 assert(Permissions(table).substr(0,3)=="r--" && Permissions(table+page).substr(0,3)=="---");
 *(float*)event=100;*(uintptr_t*)(event+8)=(uintptr_t)pawn;*(uintptr_t*)(event+16)=(uintptr_t)fall;*(int*)(event+24)=1;
 hook(pawn,fn,event);assert(forwarded==1);auto hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Fall damage" && hits[0].fatalConfirmed && hits[0].causeSettled && hits[0].deathEvent);
 auto actualFall=hits.front();
 hook(pawn,fn,event);assert(forwarded==2 && Thc::DeathTelemetry::Drain().empty());
 *(uintptr_t*)(event+8)=(uintptr_t)goblin;*(uintptr_t*)(event+16)=0;*(int*)(event+24)=2;
 hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Runt Goblin");auto actualGoblin=hits.front();
 // Environment self-instigation without a named source never becomes suicide.
 *(uintptr_t*)(event+8)=(uintptr_t)pawn;*(int*)(event+24)=3;hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && !hits[0].causeSettled);
 // Kill volumes may use a zero amount; the actual death event is the witness.
 *(float*)event=0;*(uintptr_t*)(event+16)=(uintptr_t)fall;*(int*)(event+24)=4;hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Fall damage");
 // Environmental actors can be the instigator, not just the damage Source.
 *(float*)event=100;*(uintptr_t*)(event+8)=(uintptr_t)fall;*(uintptr_t*)(event+16)=0;*(int*)(event+24)=7;
 hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Fall damage" && hits[0].causeSettled);
 // A genuine death RPC may carry an unused/sentinel/instant-kill amount.
 int sentinelID=20;
 for(float amount:{-1.0f,std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  *(float*)event=amount;*(uintptr_t*)(event+8)=(uintptr_t)fall;*(uintptr_t*)(event+16)=0;*(int*)(event+24)=sentinelID++;
  Thc::ReflectedDamage::Snapshot snapshot;std::string readReason;
  assert(!Thc::ReflectedDamage::Read((uintptr_t)event,Thc::DeathTelemetry::Get().layout,snapshot,readReason));
  hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Fall damage");
  assert(Thc::DeathTelemetry::Status()["lastUnusualAmount"]==true);
 }
 // An NPC controller is resolved only through its reciprocal pawn binding.
 *(uintptr_t*)(controller+DomEngine::Offsets::Controller_Pawn)=(uintptr_t)goblin;
 *(uintptr_t*)(goblin+DomEngine::Offsets::Pawn_Controller)=(uintptr_t)controller;
 *(float*)event=100;*(uintptr_t*)(event+8)=(uintptr_t)controller;*(uintptr_t*)(event+16)=0;*(int*)(event+24)=6;
 hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].cause=="Runt Goblin");
 int before=forwarded;hook(pawn,nullptr,nullptr);assert(forwarded==before+1 && Thc::DeathTelemetry::Drain().empty());
 *(int32_t*)(pawn+DomEngine::Offsets::UObject_Index)=99;hook(pawn,fn,event);assert(Thc::DeathTelemetry::Drain().empty());*(int32_t*)(pawn+DomEngine::Offsets::UObject_Index)=42;
 *(uintptr_t*)(event+16)=3;*(int*)(event+24)=5;hook(pawn,fn,event);hits=Thc::DeathTelemetry::Drain();
 assert(hits.size()==1 && hits[0].cause=="Runt Goblin" && Thc::DeathTelemetry::Status()["lastSourceUnavailable"]==true);
 *(uintptr_t*)(event+16)=(uintptr_t)fall;throwCapture=true;before=forwarded;hook(pawn,fn,event);assert(forwarded==before+1);throwCapture=false;
 auto rejection=Thc::DeathTelemetry::Status()["lastRejection"];assert(!rejection.get<std::string>().empty());
 Thc::DeathTelemetry::Update(players,layout);assert(Thc::DeathTelemetry::Status()["lastRejection"]==rejection);
 Thc::DeathTelemetry::Update({},layout);before=forwarded;hook(pawn,fn,event);assert(forwarded==before+1 && Thc::DeathTelemetry::Drain().empty());
 // A captured event survives a respawn/leave binding update before draining.
 Thc::DeathTelemetry::Update(players,layout);*(int*)(event+24)=100;
 hook(pawn,fn,event);Thc::DeathTelemetry::Update({},layout);
 hits=Thc::DeathTelemetry::Drain();assert(hits.size()==1 && hits[0].deathSequence>0);
 assert(Thc::DeathTelemetry::Bindings().empty());
 // Delayed witness settles one death after the snapshot; no premature unknown post.
 char root[]="/tmp/thc-telemetry-XXXXXX";assert(mkdtemp(root));Thc::Store store(root);assert(store.Load());
 store.Tick({p},true,1000,10,{},3);p.health=0;p.isDead=true;
 assert(store.Tick({p},true,1001,11,{},3).empty());
 Thc::Hit hit=actualFall;hit.observed=12;
 auto events=store.Tick({p},true,1002,12,{hit},3);assert(events.size()==1 && events[0]["details"]["cause"]=="Fall damage" && events[0]["time"]==1001);
 assert(store.Tick({p},true,1003,13,{hit},3).empty());
 p.isDead=false;p.health=100;store.Tick({p},true,1004,14,{},3);p.isDead=true;p.health=0;
 assert(store.Tick({p},true,1005,15,{},3).empty());assert(store.Tick({p},true,1006,16,{},3).empty());
 events=store.Tick({p},true,1009,19,{},3);assert(events.size()==1 && events[0]["details"]["cause"]=="unattributed");
 // A captured mob telemetry record also drives the real per-cause ledger.
 p.isDead=false;p.health=100;store.Tick({p},true,1010,20,{},3);p.isDead=true;p.health=0;actualGoblin.observed=21;
 events=store.Tick({p},true,1011,21,{actualGoblin},3);assert(events.size()==1 && events[0]["details"]["cause"]=="Runt Goblin");
 assert(store.Read()["characters"][p.characterGuid]["deathsByCause"]["Fall damage"]==1);
 assert(store.Read()["characters"][p.characterGuid]["deathsByCause"]["Runt Goblin"]==1);
 // Respawn before the grace expires flushes the old death once.
 p.isDead=false;p.health=100;store.Tick({p},true,1020,30,{},3);p.isDead=true;p.health=0;
 assert(store.Tick({p},true,1021,31,{},3).empty());p.isDead=false;p.health=100;
 events=store.Tick({p},true,1022,32,{},3);assert(events.size()==1 && events[0]["type"]=="death");
 p.isDead=true;p.health=0;assert(store.Tick({p},true,1023,33,{},3).empty());
 assert(store.Tick({},true,1024,34,{},3).empty());events=store.Tick({},true,1025,35,{},3);
 assert(events.size()==2 && events[0]["type"]=="death" && events[1]["type"]=="leave");
 std::filesystem::remove_all(root);Thc::DeathTelemetry::Stop();munmap(memory,page*2);
 std::cout<<"PASS: actual death telemetry layout discovery, fall and goblin sources, zero-amount death, self-not-suicide, duplicate/stale/malformed refusal, original forwarding and delayed single-post settlement\n";
}
