#include "engine/world_save.cpp"
#include <cassert>
#include <iostream>
#include <map>
namespace {
alignas(uintptr_t) unsigned char saveFn[0xE0]{},postFn[0xE0]{},live[0x80]{},duplicate[0x80]{},cdo[0x80]{},owner[0x80]{};
std::vector<uintptr_t> objects;int saves=0,originalCalls=0;bool queueReady=true,changeIndex=false;double pumpAge=0;uintptr_t world=0x123456;
void Original(void*,void*,void*){++originalCalls;}
void Complete(bool success,uintptr_t context=0,bool validFrame=true){
 char16_t text[]=u"Synthetic World";alignas(uintptr_t) unsigned char params[24]{};FString value{text,16,16};memcpy(params,&value,sizeof(value));params[16]=success;
 alignas(uintptr_t) unsigned char frame[0x100]{};*(uintptr_t*)(frame+0x10)=validFrame?(uintptr_t)postFn:0;*(uintptr_t*)(frame+0x28)=(uintptr_t)params;
 auto hook=(void(*)(void*,void*,void*))*(uintptr_t*)(postFn+0xD8);hook((void*)(context?context:(uintptr_t)live),frame,nullptr);
}
}
namespace Mem {bool Readable(const void* p,size_t size){return (uintptr_t)p>=0x10000 && (uintptr_t)p+size>=(uintptr_t)p;}bool InImage(const void* p){return p==(void*)&Original;}}
namespace DomEngine {
Engine* g_Engine=nullptr;bool Engine::Initialize(){initialized=true;return true;}
uintptr_t Engine::FindFunction(const char*,const char* name)const{return std::string(name)=="SaveGame"?(uintptr_t)saveFn:(uintptr_t)postFn;}
int32_t Engine::GetObjectCount()const{return objects.size();}uintptr_t Engine::GetObjectByIndex(int32_t i)const{return objects.at(i);}
bool Engine::IsA(uintptr_t p,const char* type)const{return (std::string(type)=="PersistenceSubsystem"&&(p==(uintptr_t)live||p==(uintptr_t)cdo||p==(uintptr_t)duplicate)) || (std::string(type)=="GameInstance"&&p==(uintptr_t)owner);}
std::string Engine::GetObjectName(uintptr_t p)const{return p==(uintptr_t)cdo?"Default__PersistenceSubsystem":"PersistenceSubsystem_1";}
uintptr_t Engine::GetWorld()const{return world;}
std::string Engine::ReadFString(uintptr_t)const{return "Synthetic World";}
}
namespace GameThread {
double PumpAgeSeconds(){return pumpAge;}bool IsReady(){return true;}bool FrameLayoutReady(){return true;}bool HasProcessEvent(){return true;}
uintptr_t FrameNodeOffset(){return 0x10;}uintptr_t FrameLocalsOffset(){return 0x28;}
bool RunSync(const std::function<void()>& work,int){if(!queueReady)return false;if(changeIndex)++*(int32_t*)(live+0x0C);work();return true;}
void CallFunction(uintptr_t object,uintptr_t fn,void* params){assert(object==(uintptr_t)live && fn==(uintptr_t)saveFn && *(bool*)params);++saves;}
}
int main(){
 WorldSave::Initialize();
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;engine.Initialize();
 *(uint32_t*)(saveFn+0xB0)=0x400;*(uint32_t*)(saveFn+0xB4)=(1u<<16)|1;
 *(uint32_t*)(postFn+0xB0)=0x400;*(uint32_t*)(postFn+0xB4)=(17u<<16)|2;*(uintptr_t*)(postFn+0xD8)=(uintptr_t)&Original;
 *(uintptr_t*)(live+0x20)=(uintptr_t)owner;*(uintptr_t*)(duplicate+0x20)=(uintptr_t)owner;*(int32_t*)(live+0x0C)=17;
 objects={(uintptr_t)cdo,(uintptr_t)live};WorldSave::Discover();assert(WorldSave::Available());pumpAge=11;assert(!WorldSave::Available());pumpAge=0;auto installed=*(uintptr_t*)(postFn+0xD8);WorldSave::Discover();assert(*(uintptr_t*)(postFn+0xD8)==installed);
 assert(WorldSave::Request()["status"]=="accepted" && saves==1);assert(WorldSave::Status()["busy"]==true);assert(WorldSave::Request().contains("error") && saves==1);
 Complete(true,0,false);assert(WorldSave::Status()["busy"]==true && originalCalls==1);
 Complete(true,(uintptr_t)duplicate);assert(WorldSave::Status()["busy"]==true && originalCalls==2);
 Complete(true);assert(originalCalls==3 && WorldSave::Status()["request"]["state"]=="reported_success");assert(WorldSave::Status()["request"]["completion"]["success"]==true);assert(WorldSave::TakeCompletion()["success"]==true && WorldSave::TakeCompletion().empty());
 assert(WorldSave::Request()["status"]=="accepted");Complete(false);assert(WorldSave::Status()["request"]["state"]=="reported_failure");
 *(uint32_t*)(live+0x08)=0x8000;assert(!WorldSave::Available() && WorldSave::Request().contains("error"));*(uint32_t*)(live+0x08)=0;
 *(uintptr_t*)(postFn+0xD8)=(uintptr_t)&Original;assert(!WorldSave::Available());*(uintptr_t*)(postFn+0xD8)=installed;
 queueReady=false;int prior=saves;assert(WorldSave::Request().contains("error") && saves==prior);queueReady=true;
 changeIndex=true;assert(WorldSave::Request().contains("error") && saves==prior);changeIndex=false;*(int32_t*)(live+0x0C)=17;
 world=0;assert(WorldSave::Request().contains("error") && saves==prior);world=0x123456;
 assert(WorldSave::Request()["status"]=="accepted");WorldSave::State().deadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);assert(WorldSave::Status()["request"]["state"]=="unconfirmed");Complete(true);assert(WorldSave::Status()["request"]["state"]=="unconfirmed");
 objects.push_back((uintptr_t)duplicate);WorldSave::Discover();assert(!WorldSave::Available());objects.pop_back();WorldSave::Discover();assert(WorldSave::Available());
 *(uint32_t*)(saveFn+0xB4)=(8u<<16)|1;assert(!WorldSave::Available() && WorldSave::Request().contains("error"));
 WorldSave::TakeCompletion();WorldSave::Stop();int previous=originalCalls;Complete(true);assert(originalCalls==previous+1 && WorldSave::TakeCompletion().empty() && !WorldSave::Available());
 std::cout<<"PASS: shutdown-safe observer, native save signature, unique live subsystem, idempotent observer, one request, original forwarding, completion/failure, wrong frame/context refusal, timeout honesty and stale/queue/world refusal\n";
}
