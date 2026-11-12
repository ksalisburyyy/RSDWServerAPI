#include "thc/combat.cpp"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
std::vector<std::pair<uintptr_t,size_t>> regions;
void Register(void* p,size_t n){regions.push_back({(uintptr_t)p,n});}
alignas(uintptr_t) unsigned char function[0xE0]{},fields[6][0x80]{},actor[0x1900]{},component[0x150]{};
const char* names[]={"None","DamagedActor","BaseDamage","EventInstigator","DamageCauser","DamageTypeClass","ReturnValue"};
int originals=0;
void Original(void*,void*,void* result){++originals;*(float*)result=25;}
}
namespace Mem {
bool Readable(const void* pointer,size_t size){auto p=(uintptr_t)pointer;for(auto r:regions)if(p>=r.first && p-r.first<=r.second && size<=r.second-(p-r.first))return true;return false;}
bool InImage(const void* p){return p==(void*)&Original;}
}
namespace GameThread {
bool FrameLayoutReady(){return true;}uintptr_t FrameNodeOffset(){return 0x10;}uintptr_t FrameLocalsOffset(){return 0x28;}
}
namespace DomEngine {
Engine* g_Engine=nullptr;
std::string Engine::NameToString(uint32_t id,uint32_t)const{return id<7?names[id]:"";}
uintptr_t Engine::FindFunction(const char*,const char* name)const{return std::string(name)=="ApplyDamage"?(uintptr_t)function:0;}
bool Engine::IsA(uintptr_t object,const char* name)const{return (object==(uintptr_t)actor && std::string(name)=="Actor") || (object==(uintptr_t)component && std::string(name)=="HealthComponent");}
std::string Engine::GetObjectName(uintptr_t)const{return "";}
std::string Engine::ReadFString(uintptr_t)const{return "";}
std::string Engine::GetObjectClassName(uintptr_t object)const{return object==(uintptr_t)actor?"Goblin":"Sword";}
}
int main(){
 for(auto r:{std::pair<void*,size_t>{function,sizeof(function)},{fields,sizeof(fields)},{actor,sizeof(actor)},{component,sizeof(component)}})Register(r.first,r.second);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;
 *(uint32_t*)(function+0xB4)=44u<<16;*(uint32_t*)(function+0xB0)=0x400;*(uintptr_t*)(function+0xD8)=(uintptr_t)&Original;*(uintptr_t*)(function+0x50)=(uintptr_t)fields[0];
 int offsets[]={0,8,16,24,32,40};
 for(int i=0;i<6;++i){*(uint32_t*)(fields[i]+0x28)=(uint32_t)i+1;*(int32_t*)(fields[i]+0x4C)=offsets[i];if(i<5)*(uintptr_t*)(fields[i]+0x20)=(uintptr_t)fields[i+1];}
 Thc::Combat::Parameters layout;*(int32_t*)(fields[5]+0x4C)=4;assert(!Thc::Combat::Discover((uintptr_t)function,layout));*(int32_t*)(fields[5]+0x4C)=40;
 assert(Thc::Combat::Discover((uintptr_t)function,layout));assert(layout.fields.at("BaseDamage")==8);Thc::Combat::Poll();assert(Thc::Combat::Status()["armedFunctions"]==1);
 auto hook=*(uintptr_t*)(function+0xD8);assert(hook!=(uintptr_t)&Original);Thc::Combat::Poll();assert(*(uintptr_t*)(function+0xD8)==hook);
 *(int32_t*)(actor+DomEngine::Offsets::UObject_Index)=42;*(uintptr_t*)(actor+0x280)=(uintptr_t)component;*(uintptr_t*)(component+DomEngine::Offsets::UObject_Outer)=(uintptr_t)actor;
 *(float*)(component+DomEngine::Offsets::Health_MaxHealth)=100;*(float*)(component+DomEngine::Offsets::Health_AuthoritativeHealth)=0;
 alignas(uintptr_t) unsigned char frame[0x100]{},params[48]{};float returned=0;Register(frame,sizeof(frame));Register(params,sizeof(params));Register(&returned,sizeof(returned));
 *(uintptr_t*)(frame+0x10)=(uintptr_t)function;*(uintptr_t*)(frame+0x28)=(uintptr_t)params;*(uintptr_t*)params=(uintptr_t)actor;*(uintptr_t*)(params+16)=0x12345678;
 ((void(*)(void*,void*,void*))hook)(nullptr,frame,&returned);assert(originals==1 && returned==25);
 DomEngine::PlayerInfo player;player.controllerPtr=0x12345678;player.characterGuid=std::string(32,'A');
 auto now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 auto hits=Thc::Combat::Drain({player},now);assert(hits.size()==1 && hits[0].npcDeath && hits[0].attackerGuid==player.characterGuid);
 // A frame for a different function must never be decoded, but is forwarded.
 *(uintptr_t*)(frame+0x10)=0;((void(*)(void*,void*,void*))hook)(nullptr,frame,&returned);assert(originals==2);assert(Thc::Combat::Drain({player},now).empty());
 Thc::Combat::Stop();((void(*)(void*,void*,void*))hook)(nullptr,frame,&returned);assert(originals==3);
 std::cout<<"PASS: reflected layout gates, wrong-layout refusal, native-hook idempotence, original forwarding, evidence capture and wrong-frame refusal\n";
}
