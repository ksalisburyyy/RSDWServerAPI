#include "death_telemetry.h"
#include "reflected_damage.h"
#include "creature_names.h"
#include "environment_damage.h"
#include "../engine/process_event.h"
#include "../utils/pointer_patch.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
namespace Thc::DeathTelemetry {
namespace {
using Previous=GameThread::ProcessEventFn;
struct Binding{std::string guid,name;uintptr_t pawn=0;int index=-1;};
struct State{
 std::mutex mutex;std::map<uintptr_t,Previous> previous;
 std::map<uintptr_t,Binding> bindings;
 uintptr_t function=0;uint32_t shape=0;ReflectedDamage::EventLayout layout;DamageFields::Layout reflection;int parameterOffset=0;
 std::deque<Hit> ready;std::map<uintptr_t,std::pair<int,ReflectedDamage::Snapshot>> seen;
 std::string reason="waiting for validated death telemetry and game thread",lastSourceClass,lastInstigatorClass,lastCause,lastRejection,lastStatusEvidence;
 bool lastUnusualAmount=false,lastSourceUnavailable=false,lastInstigatorUnavailable=false;
 uint64_t calls=0,captured=0,rejected=0,dropped=0,nextSequence=0;
};
State& Get(){static auto* state=new State;return *state;}
std::atomic<bool> stopped{false};
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string Environment(std::string name){
 if(name.rfind("Default__",0)==0)name.erase(0,9);
 if(name.rfind("BP_",0)==0)name.erase(0,3);
 if(name.size()>2 && name.substr(name.size()-2)=="_C")name.resize(name.size()-2);
 static const std::map<std::string,std::string> known={{"FallDamageActor","Fall damage"},{"FallDamage","Fall damage"},{"KillZVolume","Out of bounds"},{"KillVolume","Out of bounds"},{"Suicide","Suicide"},{"SuicideDamageActor","Suicide"},{"Starvation","Starvation"},{"Dehydration","Dehydration"},{"PoisonDamageActor","Poison"},{"BurningDamageActor","Fire"}};
 auto it=known.find(name);return it==known.end()?std::string{}:it->second;
}
bool SourceLive(uintptr_t source,int index){return source && index>=0 && Mem::ReadI32(source+DomEngine::Offsets::UObject_Index,-1)==index;}
std::string Cause(const Binding& victim,const ReflectedDamage::Snapshot& damage,const std::map<uintptr_t,Binding>& bindings){
 auto* e=DomEngine::g_Engine;if(!e)return {};
 if(SourceLive(damage.source,damage.sourceIndex)){
  auto cause=Environment(e->GetObjectClassName(damage.source));if(cause.empty())cause=Environment(e->GetObjectName(damage.source));
  if(!cause.empty())return cause;
 }
 uintptr_t actor=SourceLive(damage.instigator,damage.instigatorIndex)?damage.instigator:0;
 if(actor){
  auto cause=Environment(e->GetObjectClassName(actor));if(cause.empty())cause=Environment(e->GetObjectName(actor));
  if(!cause.empty())return cause;
 }
 if(e->IsA(actor,"Controller")){
  auto pawn=Mem::ReadPtr(actor+DomEngine::Offsets::Controller_Pawn);
  actor=pawn && Mem::ReadPtr(pawn+DomEngine::Offsets::Pawn_Controller)==actor?pawn:0;
 }
 if(actor && actor!=victim.pawn){
  auto player=bindings.find(actor);if(player!=bindings.end())return player->second.name;
  if(e->IsA(actor,"DominionAICharacter"))return CreatureName(e->GetObjectClassName(actor));
 }
 if((!actor || actor==victim.pawn) && SourceLive(damage.source,damage.sourceIndex) && e->IsA(damage.source,"DominionAICharacter"))return CreatureName(e->GetObjectClassName(damage.source));
 // Self-instigation is common for environmental deaths. It never implies suicide.
 return {};
}
void Observe(void* self,void* function,void* params){
 Previous previous=nullptr;
 {auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);auto it=s.previous.find(Mem::ReadPtr((uintptr_t)self));if(it!=s.previous.end())previous=it->second;}
 // Capture independently of forwarding. Every observed or unrelated event is chained once.
 try{
  auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);
  if(!stopped && (uintptr_t)function==s.function && s.function){
   ++s.calls;auto binding=s.bindings.find((uintptr_t)self);
   if(binding!=s.bindings.end() && !(Mem::ReadI32((uintptr_t)self+DomEngine::Offsets::UObject_Flags,-1)&0x18030) && Mem::ReadI32((uintptr_t)self+DomEngine::Offsets::UObject_Index,-1)==binding->second.index && Mem::ReadI32(s.function+0xB4)==(int32_t)s.shape){
    ReflectedDamage::Snapshot damage;std::string reason;
    if(ReflectedDamage::Read((uintptr_t)params+s.parameterOffset,s.layout,damage,reason,true)){
     auto last=s.seen.find((uintptr_t)self);
     if(last==s.seen.end() || last->second.first!=binding->second.index || !ReflectedDamage::SameEvent(last->second.second,damage)){
      s.seen[(uintptr_t)self]={binding->second.index,damage};
      s.lastUnusualAmount=damage.unusualAmount;s.lastSourceUnavailable=damage.sourceUnavailable;s.lastInstigatorUnavailable=damage.instigatorUnavailable;
      s.lastSourceClass=damage.source?DomEngine::g_Engine->GetObjectClassName(damage.source):"";
      s.lastInstigatorClass=damage.instigator?DomEngine::g_Engine->GetObjectClassName(damage.instigator):"";
      Hit hit;hit.victimGuid=binding->second.guid;hit.victimId=std::to_string((uintptr_t)self)+":"+std::to_string(binding->second.index);
      hit.deathSequence=++s.nextSequence;hit.observed=Now();hit.fatalConfirmed=true;hit.deathEvent=true;hit.cause=Cause(binding->second,damage,s.bindings);
      if(hit.cause.empty())hit.cause=EnvironmentDamage::Read((uintptr_t)params+s.parameterOffset,s.layout,s.reflection,s.lastStatusEvidence);
      else s.lastStatusEvidence="identified death actor";
      hit.causeSettled=!hit.cause.empty();s.lastCause=hit.cause;
      hit.causeEvidence="engine death-telemetry event with runtime-validated damage struct, bound character";
      if(s.ready.size()<256){s.ready.push_back(std::move(hit));++s.captured;}else ++s.dropped;
     }
    }else{++s.rejected;s.reason=reason;s.lastRejection=reason;}
   }else{++s.rejected;s.lastRejection="unbound character, stale pawn index or changed function signature";}
  }
 }catch(...){auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);++s.rejected;s.lastRejection="capture exception while resolving death metadata";}
 if(previous)previous(self,function,params);
}
bool Discover(const DamageFields::Layout& layout,uintptr_t& function,uint32_t& shape,int& offset,ReflectedDamage::EventLayout& event,std::string& reason){
 auto* e=DomEngine::g_Engine;if(!e || !layout.ready){reason="reflection layout unavailable";return false;}
 function=e->FindFunction("DominionPlayerCharacter","Client_SendDeathEventTelemetry");
 if(!function){reason="death telemetry function not found";return false;}
 shape=(uint32_t)Mem::ReadI32(function+0xB4);auto flags=(uint32_t)Mem::ReadI32(function+0xB0);
 if((shape&0xff)!=1 || (shape>>16)<4 || (shape>>16)>4096 || (flags&0x01000040u)!=0x01000040u){reason="death telemetry parameter count, size or client RPC flags rejected";return false;}
 auto properties=DamageFields::Fields(function,layout);
 if(properties.size()!=1){reason="death telemetry must have exactly one property";return false;}
 auto property=properties.front();ReflectedDamage::Property p;
 p.address=property;p.kind=DamageFields::Kind(property,layout);p.offset=Mem::ReadI32(property+layout.offset,-1);p.size=Mem::ReadI32(property+layout.size,-1);
 if(p.offset<0 || p.size<4 || p.offset>(int)(shape>>16)-p.size || Mem::ReadI32(property+layout.size-4)!=1){reason="death telemetry property lies outside its parameter block";return false;}
 if(!ReflectedDamage::LearnStruct(p,layout,"DominionDamageEvent",event,reason,false))return false;
 offset=p.offset;return true;
}
}
void Update(const std::vector<DomEngine::PlayerInfo>& players,const DamageFields::Layout& layout){
 if(stopped)return;
 if(players.empty()){auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);s.bindings.clear();s.seen.clear();return;}
 if(!layout.ready || !GameThread::IsReady() || !GameThread::HasProcessEvent() || GameThread::PumpAgeSeconds()<0 || GameThread::PumpAgeSeconds()>10)return;
 // Value capture: a timed-out queued task cannot retain references to Poll's stack.
 GameThread::RunSync([layout]{
  if(stopped)return;
  auto* e=DomEngine::g_Engine;if(!e)return;
  auto current=e->GetAllPlayers();std::map<uintptr_t,Binding> bindings;
  std::set<std::string> guids;std::set<uintptr_t> ambiguous;
  for(const auto& p:current){
   if(!p.pawnPtr || p.characterGuid.size()!=32 || p.characterGuid.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos || p.characterName.empty() || (Mem::ReadI32(p.pawnPtr+DomEngine::Offsets::UObject_Flags,-1)&0x18030) || !e->IsA(p.pawnPtr,"DominionPlayerCharacter"))continue;
   int index=Mem::ReadI32(p.pawnPtr+DomEngine::Offsets::UObject_Index,-1);if(index<0)continue;
   if(!guids.insert(p.characterGuid).second){for(const auto& b:bindings)if(b.second.guid==p.characterGuid)ambiguous.insert(b.first);ambiguous.insert(p.pawnPtr);}
   if(!bindings.emplace(p.pawnPtr,Binding{p.characterGuid,p.characterName,p.pawnPtr,index}).second)ambiguous.insert(p.pawnPtr);
  }
  for(auto pawn:ambiguous)bindings.erase(pawn);
  uintptr_t fn=0;uint32_t shape=0;int offset=0;ReflectedDamage::EventLayout event;std::string reason;
  bool valid=false;
  {
   auto& cached=Get();std::lock_guard<std::mutex> lock(cached.mutex);
   if(cached.function && Mem::Readable((void*)cached.function,0xE0) && (uint32_t)Mem::ReadI32(cached.function+0xB4)==cached.shape && ((uint32_t)Mem::ReadI32(cached.function+0xB0)&0x01000040u)==0x01000040u && e->GetObjectName(cached.function)=="Client_SendDeathEventTelemetry" && e->IsA(cached.layout.scriptStruct,"ScriptStruct")){
    fn=cached.function;shape=cached.shape;offset=cached.parameterOffset;event=cached.layout;valid=true;
   }
  }
  if(!valid)valid=Discover(layout,fn,shape,offset,event,reason);
  auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);
  s.bindings=std::move(bindings);
  for(const auto& p:current)if(p.hasHealth && !p.isDead && p.health>0)s.seen.erase(p.pawnPtr);
  for(auto it=s.seen.begin();it!=s.seen.end();)if(!s.bindings.count(it->first) || s.bindings.at(it->first).index!=it->second.first)it=s.seen.erase(it);else ++it;
  if(!valid){s.function=0;s.reason=reason;return;}
  s.function=fn;s.shape=shape;s.parameterOffset=offset;s.layout=event;s.reflection=layout;s.reason="validated death telemetry; waiting for death events";
  for(const auto& binding:s.bindings){
   auto pawn=binding.first;auto vtable=Mem::ReadPtr(pawn);int index=GameThread::VTableIndex();
   if(!vtable || index<0 || index>1024 || !Mem::InImage((void*)vtable))continue;
   if(s.previous.count(vtable))continue;
   auto slot=vtable+(uintptr_t)index*sizeof(uintptr_t);auto previous=Mem::ReadPtr(slot);
   if(!previous || !Mem::InImage((void*)previous)){s.reason="unrecognized existing player dispatcher; observer not installed";continue;}
   // Publish the saved dispatcher under the callback's lock before the atomic swap.
   s.previous[vtable]=(Previous)previous;
   if(!PointerPatch::Replace(slot,previous,(uintptr_t)&Observe)){s.previous.erase(vtable);s.reason="player dispatcher could not be patched";}
  }
 },1000);
}
std::vector<Hit> Drain(){
 auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);std::vector<Hit> result;
 // Identity/pawn were validated at capture. Respawning before the next poll must
 // not invalidate a genuine event from the previous pawn. Store checks freshness.
 while(!s.ready.empty()){result.push_back(std::move(s.ready.front()));s.ready.pop_front();}
 return result;
}
std::map<std::string,std::string> Bindings(){
 auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);std::map<std::string,std::string> result;
 if(stopped || !s.function)return result;
 for(const auto& pair:s.bindings){
  const auto& binding=pair.second;auto vtable=Mem::ReadPtr(pair.first);
  int index=GameThread::VTableIndex();
  if(index>=0 && index<=1024 && s.previous.count(vtable) && Mem::ReadI32(pair.first+DomEngine::Offsets::UObject_Index,-1)==binding.index && Mem::ReadPtr(vtable+(uintptr_t)index*sizeof(uintptr_t))==(uintptr_t)&Observe){auto guid=binding.guid;for(auto& c:guid)if(c>='a' && c<='f')c-=32;result[guid]=std::to_string(pair.first)+":"+std::to_string(binding.index);}
 }
 return result;
}
Json Status(){auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);return {{"functionValidated",s.function!=0},{"observedPlayerVtables",s.previous.size()},{"boundCharacters",s.bindings.size()},{"calls",s.calls},{"captured",s.captured},{"rejected",s.rejected},{"dropped",s.dropped},{"message",s.reason},{"lastSourceClass",s.lastSourceClass},{"lastInstigatorClass",s.lastInstigatorClass},{"lastCause",s.lastCause},{"lastRejection",s.lastRejection},{"lastStatusEvidence",s.lastStatusEvidence},{"lastUnusualAmount",s.lastUnusualAmount},{"lastSourceUnavailable",s.lastSourceUnavailable},{"lastInstigatorUnavailable",s.lastInstigatorUnavailable}};}
void Stop(){stopped=true;auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);s.bindings.clear();s.ready.clear();}
}
