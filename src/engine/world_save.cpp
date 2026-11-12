#include "world_save.h"
#include "dom_engine.h"
#include "process_event.h"
#include "../utils/memory.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
namespace WorldSave {
namespace {
using Thunk=void(*)(void*,void*,void*);
std::atomic<uintptr_t> saveFunction{0},postFunction{0},instance{0};
std::atomic<int> instanceIndex{-1};
std::atomic<Thunk> original{nullptr};
std::atomic<bool> stopped{false};
struct SaveState {
 std::mutex lock;
 nlohmann::json job={{"state","idle"},{"id",0},{"message","No manual save requested"}};
 nlohmann::json completion=nlohmann::json::object(),pendingEvent=nlohmann::json::object();
 uint64_t nextJob=1;uintptr_t requestedInstance=0;
 std::chrono::steady_clock::time_point deadline;
};
SaveState& State(){static SaveState state;return state;}
bool Shape(uintptr_t fn,int bytes,int count){
 if(!fn || !Mem::Readable((void*)fn,0xE0))return false;
 auto flags=(uint32_t)Mem::ReadI32(fn+0xB0),shape=(uint32_t)Mem::ReadI32(fn+0xB4);
 return (flags&0x400) && !(flags&0x40) && (shape>>16)==(uint32_t)bytes && (shape&0xff)==(uint32_t)count;
}
bool LiveInstance(uintptr_t object){
 auto* e=DomEngine::g_Engine;if(!e || !e->IsInitialized() || !object)return false;
 // Standard UObject class-default/archetype and begin/finish-destroyed flags.
 if(Mem::ReadI32(object+DomEngine::Offsets::UObject_Flags,-1)&0x18030)return false;
 if(!e->IsA(object,"PersistenceSubsystem") || e->GetObjectName(object).rfind("Default__",0)==0)return false;
 auto owner=Mem::ReadPtr(object+DomEngine::Offsets::UObject_Outer);
 return owner && e->IsA(owner,"GameInstance");
}
void Expire(SaveState& s){
 if(s.job["state"]=="pending" && std::chrono::steady_clock::now()>=s.deadline){s.job["state"]="unconfirmed";s.job["message"]="No world-save completion observed within 30 seconds; inspect the game log";}
}
void PostThunk(void* self,void* frame,void* result){
 bool observed=false,success=false;std::string slot;
 try{
  if(!stopped.load() && DomEngine::g_Engine && GameThread::FrameLayoutReady() && (uintptr_t)self==instance.load() &&
     Mem::ReadPtr((uintptr_t)frame+GameThread::FrameNodeOffset())==postFunction.load()){
   auto params=Mem::ReadPtr((uintptr_t)frame+GameThread::FrameLocalsOffset());
   if(params && Mem::Readable((void*)params,17)){
    const auto value=Mem::ReadU8(params+16);
    if(value<=1){slot=DomEngine::g_Engine->ReadFString(params);if(slot.size()<=512){success=value!=0;observed=true;}}
   }
  }
 }catch(...){}
 // Observe without swallowing, repeating or changing the game's callback.
 auto previous=original.load();if(previous)previous(self,frame,result);
 if(!observed)return;
 try{
  auto& s=State();std::lock_guard<std::mutex> guard(s.lock);Expire(s);
  s.completion={{"success",success},{"slot",slot},{"at",time(nullptr)}};s.pendingEvent=s.completion;
  if(s.job["state"]=="pending" && s.requestedInstance==(uintptr_t)self){
   s.job["state"]=success?"reported_success":"reported_failure";
   s.job["message"]=success?"Game reported a successful world save after the request":"Game reported world-save failure after the request";
   s.job["completion"]=s.completion;
  }
 }catch(...){}
}
}
void Initialize(){(void)State();}
void Stop(){stopped=true;}
void Discover(){
 if(stopped.load())return;
 auto* e=DomEngine::g_Engine;if(!e || !e->IsInitialized())return;
 const auto save=e->FindFunction("PersistenceSubsystem","SaveGame");
 const auto post=e->FindFunction("PersistenceSubsystem","PostSaveWorldState");
 // Public reflected declarations and the owner's live catalog agree:
 // SaveGame(bool bAdditionalLogging), PostSaveWorldState(FString, bool).
 if(!Shape(save,1,1) || !Shape(post,17,2)){saveFunction=0;return;}
 uintptr_t found=0;
 for(int i=0;i<e->GetObjectCount();++i){
  auto object=e->GetObjectByIndex(i);if(!LiveInstance(object))continue;
  if(found && found!=object){instance=0;return;}found=object;
 }
 instance=found;instanceIndex=found?Mem::ReadI32(found+DomEngine::Offsets::UObject_Index,-1):-1;saveFunction=save;
 if(postFunction.load()==post && Mem::ReadPtr(post+0xD8)==(uintptr_t)&PostThunk)return;
 if(postFunction.load())return; // Never steal/re-chain a changing native function.
 if(!GameThread::IsReady() || !GameThread::FrameLayoutReady() || GameThread::PumpAgeSeconds()<0 || GameThread::PumpAgeSeconds()>10)return;
 GameThread::RunSync([post]{
  if(postFunction.load() || !Shape(post,17,2))return;
  auto previous=Mem::ReadPtr(post+0xD8);
  if(!previous || !Mem::InImage((void*)previous))return;
  original=(Thunk)previous;postFunction=post;*(uintptr_t*)(post+0xD8)=(uintptr_t)&PostThunk;
 },10000);
}
bool Available(){
 return !stopped.load() && LiveInstance(instance.load()) && instanceIndex.load()>=0 && Mem::ReadI32(instance.load()+DomEngine::Offsets::UObject_Index,-1)==instanceIndex.load() && Shape(saveFunction.load(),1,1) &&
        Shape(postFunction.load(),17,2) && original.load() && Mem::ReadPtr(postFunction.load()+0xD8)==(uintptr_t)&PostThunk && GameThread::IsReady() && GameThread::FrameLayoutReady() && GameThread::HasProcessEvent() && GameThread::PumpAgeSeconds()>=0 && GameThread::PumpAgeSeconds()<=10;
}
nlohmann::json Status(){
 auto& s=State();std::lock_guard<std::mutex> guard(s.lock);Expire(s);
 const auto age=GameThread::PumpAgeSeconds();return {{"subsystemFound",LiveInstance(instance.load())},{"saveSignatureValid",Shape(saveFunction.load(),1,1)},{"completionObserverInstalled",postFunction.load()!=0 && Mem::ReadPtr(postFunction.load()+0xD8)==(uintptr_t)&PostThunk},{"pumpAgeSeconds",age<0?nlohmann::json(nullptr):nlohmann::json(age)},{"requiresRecentGameThreadActivity",true},{"available",Available()},{"busy",s.job["state"]=="pending"},{"request",s.job},{"lastCompletion",s.completion}};
}
nlohmann::json Request(){
 if(!Available())return {{"error","Native save needs a validated live subsystem, completion observer and recent game-thread activity"}};
 auto object=instance.load();auto fn=saveFunction.load();auto index=instanceIndex.load();auto& s=State();uint64_t id;
 {
  std::lock_guard<std::mutex> guard(s.lock);Expire(s);if(s.job["state"]=="pending")return {{"error","A world-save request is already pending"}};
  id=s.nextJob++;s.requestedInstance=object;s.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  s.job={{"id",id},{"state","pending"},{"requestedAt",time(nullptr)},{"message","World save requested; awaiting the game's completion callback"}};
 }
 bool invoked=false;
 const bool ran=GameThread::RunSync([&]{
  auto* e=DomEngine::g_Engine;
  if(!e || !e->GetWorld() || !LiveInstance(object) || Mem::ReadI32(object+DomEngine::Offsets::UObject_Index,-1)!=index || !Shape(fn,1,1))return;
  bool additionalLogging=true;GameThread::CallFunction(object,fn,&additionalLogging);invoked=true;
 },10000);
 std::lock_guard<std::mutex> guard(s.lock);
 if(!ran || !invoked){s.job["state"]="not_invoked";s.job["message"]="World save was not invoked: game thread or subsystem became unavailable";return {{"error",s.job["message"]}};}
 return {{"status","accepted"},{"job",id},{"message",s.job["message"]}};
}
nlohmann::json TakeCompletion(){auto& s=State();std::lock_guard<std::mutex> guard(s.lock);auto result=s.pendingEvent;s.pendingEvent=nlohmann::json::object();return result;}
}
