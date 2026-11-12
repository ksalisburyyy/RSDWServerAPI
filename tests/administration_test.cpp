#include "engine/administration.cpp"
#include <cassert>
#include <iostream>
APIConfig g_Config;
namespace {alignas(uintptr_t) unsigned char teleportFn[0xE0]{},consoleFn[0xE0]{},lib[0x80]{};std::vector<DomEngine::PlayerInfo> players;int teleports=0,commands=0;bool accepted=true,queueReady=true;uintptr_t world=0x555555;std::u16string expectedCommand=u"stat fps";}
namespace Mem {bool Readable(const void* p,size_t){return (uintptr_t)p>=0x10000;}}
namespace DomEngine {
Engine* g_Engine=nullptr;bool Engine::Initialize(){initialized=true;return true;}
uintptr_t Engine::FindFunction(const char*,const char* name)const{return std::string(name)=="K2_TeleportTo"?(uintptr_t)teleportFn:(uintptr_t)consoleFn;}
uintptr_t Engine::FindObject(const char*,bool)const{return (uintptr_t)lib;}
std::vector<PlayerInfo> Engine::GetAllPlayers()const{return players;}
bool Engine::IsA(uintptr_t p,const char*)const{return p==0x123456;}
uintptr_t Engine::GetWorld()const{return world;}
int32_t Engine::GetObjectCount()const{return 0;}
uintptr_t Engine::GetObjectByIndex(int32_t)const{return 0;}
std::string Engine::GetObjectClassName(uintptr_t)const{return {};}
std::string Engine::GetObjectName(uintptr_t)const{return {};}
}
namespace GameThread {
bool HasProcessEvent(){return true;}
bool RunSync(const std::function<void()>& work,int){if(!queueReady)return false;work();return true;}
void CallFunction(uintptr_t obj,uintptr_t fn,void* args){
 if(fn==(uintptr_t)teleportFn){assert(obj==0x123456);auto position=(FVector3d*)args;assert(position->X==10 && position->Y==20 && position->Z==30);*(bool*)((uintptr_t)args+48)=accepted;++teleports;}
 else {assert(obj==(uintptr_t)lib && fn==(uintptr_t)consoleFn);assert(*(uintptr_t*)args==0x555555);auto text=(FString*)((uintptr_t)args+8);assert(std::u16string(text->Data,text->Num-1)==expectedCommand);++commands;}
}
}
int main(){
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;engine.Initialize();DomEngine::PlayerInfo p;p.pawnPtr=0x123456;p.name="Same name";p.uniqueNetId="76561190000000000";players={p};
 std::string result;Administration::Discover();assert(!Administration::Teleport(p.uniqueNetId,10,20,30,result));
 *(uint32_t*)(teleportFn+0xB0)=0x400;*(uint32_t*)(teleportFn+0xB4)=(49u<<16)|3;
 *(uint32_t*)(consoleFn+0xB0)=0x400;*(uint32_t*)(consoleFn+0xB4)=(32u<<16)|3;Administration::Discover();
 assert(Administration::Teleport(p.uniqueNetId,10,20,30,result));assert(teleports==1);
 assert(!Administration::Teleport(p.uniqueNetId,NAN,20,30,result));assert(teleports==1);accepted=false;assert(!Administration::Teleport(p.uniqueNetId,10,20,30,result));
 auto q=p;q.uniqueNetId="76561190000000001";players.push_back(q);assert(!Administration::Teleport("Same name",10,20,30,result));
 assert(Administration::Console("stat fps",result));assert(commands==1);assert(!Administration::Console("stat fps\nquit",result));
 assert(!Administration::Console(std::string("stat fps\0quit",13),result) && commands==1);
 assert(!Administration::Console(std::string("\xff",1),result) && commands==1);
 assert(!Administration::Console(std::string("\xc3",1),result) && commands==1);
 expectedCommand=u"say café 🌙";assert(Administration::Console("say café 🌙",result) && commands==2);
 world=0;assert(!Administration::Console("stat fps",result) && commands==2);world=0x555555;
 queueReady=false;assert(!Administration::Console("stat fps",result) && commands==2);queueReady=true;
 *(uint32_t*)(consoleFn+0xB4)=(24u<<16)|3;assert(!Administration::Console("stat fps",result) && commands==2);
 *(uint32_t*)(teleportFn+0xB4)=(33u<<16)|3;assert(!Administration::Capabilities()["teleport"].get<bool>());
 assert(Administration::Functions("save",0)["functions"].empty());
 std::cout<<"PASS: console Unicode, NUL/invalid UTF-8 refusal, world/queue failure, native teleport/console reflection gates, coordinate marshalling, game rejection, ambiguity, invalid input and wrong layout refusal\n";
}
