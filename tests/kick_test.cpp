#include "engine/kick.cpp"
#include "engine/bans.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>
APIConfig g_Config;
namespace {std::vector<DomEngine::PlayerInfo> players;int kicks=0;std::u16string kickReason;}
namespace DomEngine {
Engine* g_Engine=nullptr;
bool Engine::Initialize(){initialized=true;return true;}
std::vector<PlayerInfo> Engine::GetAllPlayers()const{return players;}
uintptr_t Engine::FindFunction(const char*,const char* name)const{return std::string(name)=="KickPlayerController"?1:2;}
uintptr_t Engine::FindObject(const char*,bool)const{return 3;}
}
namespace NativeCall {
bool IsNative(uintptr_t){return true;}
bool Invoke(uintptr_t,uintptr_t function,void* params,uintptr_t offset){
 if(function==2){auto p=(DomEngine::ConvStringToTextParams*)params;kickReason.assign(p->InString.Data,p->InString.Num-1);return true;}
 assert(function==1 && offset==0x18);*(bool*)((uintptr_t)params+offset)=true;++kicks;players.clear();return true;
}
}
namespace GameThread {
bool IsReady(){return true;}const std::string& Status(){static std::string s="ready";return s;}
bool RunSync(const std::function<void()>& work,int){work();return true;}
}
int main(){
 g_Config.enableLogging=false;char path[]="/tmp/thc-kick-XXXXXX";assert(mkdtemp(path));setenv("RSDWAPI_DIR",path,1);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;engine.Initialize();DomBans::Load();
 DomEngine::PlayerInfo p;p.name="Player";p.characterName="Character";p.uniqueNetId="76561190000000000";p.controllerPtr=0x123456;p.playerId=1;
 players={p};std::string error;assert(engine.KickPlayer(p.uniqueNetId,"Test café",error));assert(kicks==1 && kickReason==u"Test café");assert(DomBans::All().empty());
 players={p};DomBans::Enforce();assert(kicks==1 && players.size()==1); // A kicked character may reconnect.
 assert(DomBans::Ban(p.uniqueNetId,"ban",error));assert(kicks==2 && players.empty());
 players={p};DomBans::Enforce();assert(kicks==3 && players.empty()); // Bans still remove reconnects.
 assert(DomBans::Unban(p.uniqueNetId,error));players={p};DomBans::Enforce();assert(kicks==3 && players.size()==1);
 // Duplicate names must never select the first player or the first ban.
 auto duplicate=p;duplicate.uniqueNetId="76561190000000001";duplicate.playerId=2;
 players={p,duplicate};assert(!DomBans::Ban("Character","test",error) && error.find("ambiguous")!=std::string::npos);
 assert(!DomBans::IsBanned(p.uniqueNetId) && !DomBans::IsBanned(duplicate.uniqueNetId));
 players={p};assert(DomBans::Ban(p.uniqueNetId,"test",error));
 players={duplicate};assert(DomBans::Ban(duplicate.uniqueNetId,"test",error));
 assert(!DomBans::Unban("Character",error) && error.find("ambiguous")!=std::string::npos);
 assert(DomBans::IsBanned(p.uniqueNetId) && DomBans::IsBanned(duplicate.uniqueNetId));
 assert(DomBans::Unban(p.uniqueNetId,error));assert(DomBans::IsBanned(duplicate.uniqueNetId));
 std::filesystem::remove_all(path);
 std::cout<<"PASS: real kick code disconnects once, no session block on reconnect, bans persist/enforce, unban allows reconnect, Unicode kick reason\n";
}
