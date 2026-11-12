#include "administration.h"
#include "dom_engine.h"
#include "process_event.h"
#include "../utils/memory.h"
#include "../config/config.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <codecvt>
#include <locale>
namespace Administration {
namespace {
std::atomic<uintptr_t> teleport{0},execute{0},library{0};
bool Shape(uintptr_t fn,int bytes,int count){
    uint32_t shape=(uint32_t)Mem::ReadI32(fn+0xB4),flags=(uint32_t)Mem::ReadI32(fn+0xB0);
    return fn && (flags&0x400u) && ((shape>>16)&0xffff)==(uint32_t)bytes && (shape&0xff)==(uint32_t)count;
}
std::string Lower(std::string s){for(char& c:s)c=(char)tolower((unsigned char)c);return s;}
}
void Discover(){
 auto* e=DomEngine::g_Engine;if(!e || !e->IsInitialized())return;
 auto t=e->FindFunction("Actor","K2_TeleportTo");
 // Linux FVector/FRotator are doubles: destination 0x00, rotation 0x18,
 // bool return 0x30. Refuse a different reflected signature.
 if(Shape(t,49,3))teleport=t;
 auto c=e->FindFunction("KismetSystemLibrary","ExecuteConsoleCommand");
 auto obj=e->FindObject("KismetSystemLibrary",true);
 if(Shape(c,32,3) && obj){execute=c;library=obj;}
}
nlohmann::json Capabilities(){return {{"teleport",Shape(teleport,49,3)&&GameThread::HasProcessEvent()},{"console",Shape(execute,32,3)&&library&&GameThread::HasProcessEvent()},{"saveWorld",!g_Config.worldSaveCommand.empty() && Shape(execute,32,3) && library && GameThread::HasProcessEvent()}};}
bool Teleport(const std::string& id,double x,double y,double z,std::string& result){
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||fabs(x)>1e9||fabs(y)>1e9||fabs(z)>1e9){result="invalid destination coordinates";return false;}
 auto* e=DomEngine::g_Engine;if(!e || !e->IsInitialized() || !Shape(teleport,49,3) || !GameThread::HasProcessEvent()){result="validated teleport function unavailable";return false;}
 DomEngine::PlayerInfo target;int matches=0;
 for(const auto& p:e->GetAllPlayers())if(p.uniqueNetId==id || p.characterGuid==id || std::to_string(p.playerId)==id || p.characterName==id || p.name==id){target=p;++matches;}
 if(matches!=1 || !target.pawnPtr){result=matches>1?"ambiguous player; use network ID":"spawned player not found";return false;}
 struct Params{FVector3d destination;FVector3d rotation;bool success;};static_assert(offsetof(Params,success)==48);
 Params params{{x,y,z},{target.pitch,target.yaw,target.roll},false};
 bool ran=GameThread::RunSync([&]{if(e->IsA(target.pawnPtr,"Actor"))GameThread::CallFunction(target.pawnPtr,teleport,&params);},10000);
 if(!ran || !params.success){result="teleport timed out or the game refused the destination";return false;}
 result="game accepted teleport";return true;
}
bool Console(const std::string& command,std::string& result){
 auto* e=DomEngine::g_Engine;
 if(command.empty() || command.size()>1024 || command.find_first_of("\r\n")!=std::string::npos || command.find('\0')!=std::string::npos){result="one console command of at most 1024 bytes required";return false;}
 if(!e || !e->IsInitialized() || !Shape(execute,32,3) || !library || !GameThread::HasProcessEvent()){result="validated console function unavailable";return false;}
 auto world=e->GetWorld();if(!world){result="game world unavailable";return false;}
 std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;std::u16string text;
 try{text=convert.from_bytes(command);if(convert.converted()!=command.size()){result="console command must be valid UTF-8";return false;}}catch(const std::range_error&){result="console command must be valid UTF-8";return false;}
 struct Params{uintptr_t world;FString command;uintptr_t player;};static_assert(sizeof(Params)==32);
 Params params{world,{const_cast<char16_t*>(text.c_str()),(int32_t)text.size()+1,(int32_t)text.size()+1},0};
 if(!GameThread::RunSync([&]{GameThread::CallFunction(library,execute,&params);},10000)){result="console invocation timed out";return false;}
 result="console command invoked; inspect the game log for its result";return true;
}
nlohmann::json Functions(const std::string& search,int after){
 auto* e=DomEngine::g_Engine;auto out=nlohmann::json::array();int next=std::max(0,after);
 if(!e || !e->IsInitialized())return {{"functions",out},{"nextIndex",next},{"complete",false}};
 auto wanted=Lower(search);int count=e->GetObjectCount();auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(150);
 for(;next<count;++next){
  if((next&255)==0 && std::chrono::steady_clock::now()>deadline)break;
  auto obj=e->GetObjectByIndex(next);if(!obj || e->GetObjectClassName(obj)!="Function")continue;
  auto name=e->GetObjectName(obj),owner=e->GetObjectName(Mem::ReadPtr(obj+DomEngine::Offsets::UObject_Outer));
  if(Lower(owner+"."+name).find(wanted)==std::string::npos)continue;
  uint32_t shape=(uint32_t)Mem::ReadI32(obj+0xB4);
  out.push_back({{"owner",owner},{"name",name},{"parameterBytes",(shape>>16)&0xffff},{"parameterCount",shape&0xff},{"native",(Mem::ReadI32(obj+0xB0)&0x400)!=0}});
  if(out.size()>=100){++next;break;}
 }
 return {{"functions",out},{"nextIndex",next},{"complete",next>=count}};
}
}
