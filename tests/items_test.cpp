// Real item selection and giving over synthetic engine objects; no game is run.
#include "engine/items.cpp"
#include <cassert>
#include <iostream>
#include <map>
APIConfig g_Config;
namespace {
alignas(uintptr_t) unsigned char give[0xE0]{},library[0x80]{},first[0x80]{},second[0x80]{},bar[0x200]{},ore[0x200]{};
std::map<uintptr_t,std::string> names;
std::vector<uintptr_t> objects;
std::vector<DomEngine::PlayerInfo> players;
bool queueReady=true,accepted=true;
int calls=0;uintptr_t expectedController=0;int expectedCount=2;
}
namespace Mem {bool Readable(const void* p,size_t size){return (uintptr_t)p>=0x10000 && (uintptr_t)p+size>=(uintptr_t)p;}}
namespace DomEngine {
Engine* g_Engine=nullptr;bool Engine::Initialize(){initialized=true;return true;}
uintptr_t Engine::FindFunction(const char*,const char* name)const{return std::string(name)=="TryGiveItemToPlayer"?(uintptr_t)give:0;}
uintptr_t Engine::FindObject(const char*,bool)const{return (uintptr_t)library;}
int32_t Engine::GetObjectCount()const{return (int32_t)objects.size();}
uintptr_t Engine::GetObjectByIndex(int32_t i)const{return i>=0 && (size_t)i<objects.size()?objects[i]:0;}
std::string Engine::GetObjectName(uintptr_t p)const{return names[p];}
bool Engine::IsA(uintptr_t p,const char* type)const{return std::string(type)=="ItemData" && (p==(uintptr_t)bar || p==(uintptr_t)ore);}
std::vector<PlayerInfo> Engine::GetAllPlayers()const{return players;}
}
namespace GameThread {bool IsReady(){return true;}const std::string& Status(){static std::string s="fixture";return s;}bool RunSync(const std::function<void()>& work,int){if(!queueReady)return false;work();return true;}}
namespace NativeCall {
bool IsNative(uintptr_t p){return p && (Mem::ReadI32(p+0xB0)&0x400);}
bool Invoke(uintptr_t context,uintptr_t fn,void* raw,uintptr_t result){
 assert(context==(uintptr_t)library && fn==(uintptr_t)give && result==20);
 auto* p=(DomItems::GiveParams*)raw;assert(p->PlayerController==expectedController && p->ItemData==(uintptr_t)bar && p->Count==expectedCount);
 p->ReturnValue=accepted;++calls;return true;
}
}
int main(){
 g_Config.enableLogging=false;DomEngine::Engine engine;DomEngine::g_Engine=&engine;assert(engine.Initialize());
 names[(uintptr_t)bar]="ITEM_Resources_IronBar";names[(uintptr_t)ore]="ITEM_Resources_IronOre";objects={(uintptr_t)ore,(uintptr_t)bar};
 *(int32_t*)(bar+0x124)=50;*(int32_t*)(ore+0x124)=99;
 *(uint32_t*)(give+0xB0)=0x400;*(uint32_t*)(give+0xB4)=(21u<<16)|4;
 auto list=DomItems::ListLoaded();assert(list.size()==2 && list[0].name=="ITEM_Resources_IronBar" && list[0].maxStackSize==50);
 std::string resolved,message;assert(!DomItems::FindItem("iron",resolved));assert(DomItems::FindItem("resources_ironbar",resolved)==(uintptr_t)bar);
 DomEngine::PlayerInfo a;a.controllerPtr=(uintptr_t)first;a.name="Account A";a.characterName="Shared";a.uniqueNetId="76561190000000001";a.characterGuid=std::string(32,'A');a.playerId=1;
 auto b=a;b.controllerPtr=(uintptr_t)second;b.name="Account B";b.uniqueNetId="76561190000000002";b.characterGuid=std::string(32,'B');b.playerId=2;
 players={a,b};expectedController=(uintptr_t)second;
 assert(!DomItems::GiveItem("Shared","IronBar",2,message) && calls==0 && message.find("ambiguous")!=std::string::npos);
 assert(DomItems::GiveItem(b.uniqueNetId,"IronBar",2,message) && calls==1);
 assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",0,message) && calls==1);
 assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",100001,message) && calls==1);
 assert(!DomItems::GiveItem(b.uniqueNetId,"iron",2,message) && calls==1);
 assert(!DomItems::GiveItem("missing","IronBar",2,message) && calls==1);
 accepted=false;assert(!DomItems::GiveItem(b.characterGuid,"IronBar",2,message) && calls==2);accepted=true;
 queueReady=false;assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",2,message) && calls==2);queueReady=true;
 *(uint32_t*)(give+0xB4)=(24u<<16)|4;assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",2,message) && calls==2);
 *(uint32_t*)(give+0xB4)=(21u<<16)|3;assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",2,message) && calls==2);
 *(uint32_t*)(give+0xB4)=(21u<<16)|4;*(uint32_t*)(give+0xB0)=0;assert(!DomItems::GiveItem(b.uniqueNetId,"IronBar",2,message) && calls==2);
 std::cout<<"PASS: real item lookup/marshal, ambiguous player/item refusal, ID targeting, game rejection, queue failure, native/signature validation and invalid quantity refusal\n";
}
