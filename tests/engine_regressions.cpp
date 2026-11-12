// Real chat/ban code over fake engine objects. No game or webhooks are used.
#include "engine/chat.cpp"
#include "engine/bans.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unordered_map>
APIConfig g_Config;
namespace {
std::unordered_map<std::string,uintptr_t> functions;
std::unordered_map<uintptr_t,std::string> types;
std::vector<DomEngine::PlayerInfo> players;
int originalCalls=0,invoked=0,watched=0,relayed=0;
std::u16string invokedText;
uintptr_t expectedMessage=0,expectedComponent=0,getterFunction=0,getterResult=0;
std::string stateGuid(32,'A');uintptr_t unreadableBegin=0,unreadableEnd=0;
struct AuditEntry{std::string action,target,result;bool success;};std::vector<AuditEntry> audits;
bool allowAdmin=false;std::string kickedTarget,kickedReason,giveTarget,giveItem;int giveCount=0;
bool frameReady=false;uintptr_t componentVTable=0;
void ComponentProcessEvent(void*,void*,void*);
void OriginalSend(void*,void*,void*){++originalCalls;}
}
namespace DomEngine {
Engine* g_Engine=nullptr;
bool Engine::Initialize(){initialized=true;return true;}
uintptr_t Engine::FindFunction(const char* owner,const char* name)const{return functions[std::string(owner)+"."+name];}
uintptr_t Engine::FindObject(const char*,bool)const{return 0;}
std::vector<PlayerInfo> Engine::GetAllPlayers()const{return players;}
std::string Engine::ReadGuid(uintptr_t)const{return stateGuid;}
std::string Engine::ReadUniqueNetId(uintptr_t,std::string&)const{return {};}
std::string Engine::ReadFString(uintptr_t p)const {
 auto f=(FString*)p;std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;
 return convert.to_bytes(f->Data,f->Data+f->Num-1);
}
std::string Engine::NameToString(uint32_t,uint32_t)const{return {};}
std::string Engine::GetObjectClassName(uintptr_t p)const{return types[p];}
bool Engine::IsA(uintptr_t p,const char* type)const{return types[p]==type;}
bool Engine::KickPlayer(const std::string& target,const std::string& reason,std::string& error)const{kickedTarget=target;kickedReason=reason;error="fake kick unavailable";return false;}
}
namespace Mem {
__attribute__((noinline)) bool Readable(const void* p,size_t n){auto a=(uintptr_t)p;return a>=0x10000 && a+n>=a && !(unreadableBegin && a<unreadableEnd && a+n>unreadableBegin);}
bool InImage(const void* p){return p==(void*)&OriginalSend || p==(void*)&ComponentProcessEvent || (componentVTable && p==(void*)componentVTable);}
}
namespace NativeCall {bool IsNative(uintptr_t fn){return fn==0xF00D;}bool Invoke(uintptr_t,uintptr_t fn,void* params,uintptr_t){if(fn!=0xF00D)return false;*(bool*)((uintptr_t)params+8)=allowAdmin;return true;}}
namespace Thc {void Audit(const std::string& action,const std::string& target,const std::string& result,bool success){audits.push_back({action,target,result,success});}}
namespace GameThread {
const std::string& Status(){static const std::string value="fixture";return value;}
uintptr_t ProcessEventAddress(){return 0x12345;}
bool IsReady(){return true;}bool HasProcessEvent(){return true;}bool FrameLayoutReady(){return frameReady;}
uintptr_t FrameLocalsOffset(){return 0x28;}int VTableIndex(){return 5;}
bool HookVTable(uintptr_t object,ProcessEventFn,ProcessEventFn&){assert(object==expectedComponent);++watched;return false;}
void CallFunction(uintptr_t object,uintptr_t fn,void* params){
 if(fn==getterFunction && getterFunction){*(uintptr_t*)params=getterResult;return;}
 assert(false && "broadcast must use component dispatch, not shared controller dispatch");
}
}
namespace {
void ComponentProcessEvent(void* self,void* function,void* params){
 auto fn=(uintptr_t)function,object=(uintptr_t)self;
 assert(fn==expectedMessage && object==expectedComponent);
 // The original RSDW contract uses a real recipient identity; a made-up zero
 // identity is not a tested server sender. Verify every borrowed field.
 for(size_t i=0;i<0x30;++i){assert(((unsigned char*)params)[i]==0x11);assert(((unsigned char*)params)[0x30+i]==0x22);}
 for(size_t i=0;i<0x10;++i)assert(((unsigned char*)params)[0x60+i]==0x33);
 assert(*(int32_t*)((uintptr_t)params+0x70)==42);
 assert(*(uint32_t*)((uintptr_t)params+0x74)==0xFFFFFFFF);
 auto text=(FString*)((uintptr_t)params+0x78);invokedText.assign(text->Data,text->Num-1);++invoked;
}
}
namespace GameThread {bool RunSync(const std::function<void()>& work,int){work();return true;}}
namespace DomItems {bool GiveItem(const std::string& target,const std::string& item,int32_t count,std::string& result){giveTarget=target;giveItem=item;giveCount=count;result="gave fixture item";return true;}}
int main(){
 g_Config.enableLogging=false;char path[]="/tmp/thc-engine-test-XXXXXX";assert(mkdtemp(path));setenv("RSDWAPI_DIR",path,1);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;assert(engine.Initialize());assert(!DomChat::Initialize());
 alignas(uintptr_t) unsigned char send[0xE0]{},receive[0xE0]{},system[0xE0]{},getter[0xE0]{};
 *(uintptr_t*)(send+0xD8)=(uintptr_t)&OriginalSend;
 functions["PlayerChatComponent.Server_SendChatMessage"]=(uintptr_t)send;
 assert(DomChat::Initialize() && DomChat::IsHooked() && !DomChat::CanBroadcast());auto hook=*(uintptr_t*)(send+0xD8);
 *(uint32_t*)(receive+0xB0)=0x01040CC1;*(uint32_t*)(receive+0xB4)=(0x88u<<16)|1;
 functions["PlayerChatComponent.Client_ReceiveChatMessage"]=(uintptr_t)receive;
 assert(DomChat::Initialize() && DomChat::CanBroadcast());assert(*(uintptr_t*)(send+0xD8)==hook);
 ((void(*)(void*,void*,void*))hook)(nullptr,nullptr,nullptr);assert(originalCalls==1);
 std::string error;assert(!DomChat::Broadcast("hello",error) && error=="no players to broadcast to");
 alignas(uintptr_t) unsigned char controller[0x1800]{},state[0x500]{},component[0x160]{},wrong[0x160]{},other[0x160]{},vtable[0x80]{};
 auto c=(uintptr_t)controller,chat=(uintptr_t)component,bad=(uintptr_t)wrong;
 types[chat]="PlayerChatComponent";types[bad]="BuildInteractionComponent";types[(uintptr_t)other]="PlayerChatComponent";
 *(uintptr_t*)(component+0x20)=c;*(uintptr_t*)(other+0x20)=c;*(uintptr_t*)component=(uintptr_t)vtable;
 componentVTable=(uintptr_t)vtable;*(uintptr_t*)(vtable+5*sizeof(uintptr_t))=(uintptr_t)&ComponentProcessEvent;
 *(int32_t*)(component+0x0C)=12;
 *(uintptr_t*)(controller+0x1288)=bad; // Exact failure observed in the live log.
 *(uintptr_t*)(controller+0x1420)=chat;
 DomEngine::PlayerInfo p;p.controllerPtr=c;p.playerStatePtr=(uintptr_t)state;p.playerId=42;p.characterName="Real character";p.characterGuid=std::string(32,'A');
 memset(state+DomEngine::Offsets::PlayerState_UniqueID,0x11,0x30);
 memset(state+DomEngine::Offsets::DomPlayerState_NetIdSecondary,0x22,0x30);
 memset(state+DomEngine::Offsets::DomPlayerState_CharacterGuid,0x33,0x10);
 *(int32_t*)(state+DomEngine::Offsets::PlayerState_PlayerId)=42;
 players={p};expectedComponent=chat;expectedMessage=(uintptr_t)receive;
 assert(DomChat::Broadcast("[Server] Hello café 🌍",error));assert(invoked==1 && invokedText==u"[Server] Hello café 🌍");
 DomChat::g_SystemMessageFunction=(uintptr_t)system;
 DomChat::Poll();assert(watched==1); // Never hooks the BuildInteractionComponent.
 // System tags are never treated as arbitrary text, even at the same block size.
 DomChat::g_SystemMessageFunction=(uintptr_t)system;
 *(uint32_t*)(system+0xB0)=0x01040CC1;*(uint32_t*)(system+0xB4)=(8u<<16)|1;
 assert(DomChat::BroadcastFunction()==(uintptr_t)receive);
 *(uint32_t*)(system+0xB4)=(0x88u<<16)|1;assert(DomChat::BroadcastFunction()==(uintptr_t)receive);
 assert(DomChat::Broadcast("[Server] system",error) && invoked==2);
 // An installed observer must use its saved component original, never recurse.
 DomChat::g_HookedVTables[componentVTable]=&ComponentProcessEvent;
 *(uintptr_t*)(vtable+5*sizeof(uintptr_t))=(uintptr_t)&DomChat::HookedChatProcessEvent;
 assert(DomChat::ComponentDispatch(chat)==&ComponentProcessEvent);
 DomChat::g_HookedVTables.clear();assert(!DomChat::ComponentDispatch(chat));
 *(uintptr_t*)(vtable+5*sizeof(uintptr_t))=0;assert(!DomChat::ComponentDispatch(chat));
 *(uintptr_t*)(vtable+5*sizeof(uintptr_t))=1;assert(!DomChat::ComponentDispatch(chat));
 *(uintptr_t*)(vtable+5*sizeof(uintptr_t))=(uintptr_t)&ComponentProcessEvent;
 // Invalid RPC flags/size never become an announcement target.
 DomChat::g_ReceiveFunction=0;*(uint32_t*)(system+0xB4)=8u<<16;assert(!DomChat::CanBroadcast());
 *(uint32_t*)(system+0xB4)=(0x88u<<16)|1;assert(!DomChat::CanBroadcast());
 DomChat::g_ReceiveFunction=(uintptr_t)receive;
 *(uint32_t*)(receive+0xB4)=(0x88u<<16)|2;assert(!DomChat::CanBroadcast());
 *(uint32_t*)(receive+0xB4)=(0x88u<<16)|1;
 *(uint32_t*)(receive+0xB0)=0x01000040;assert(!DomChat::CanBroadcast());
 *(uint32_t*)(receive+0xB0)=0x01040CC1;
 getterFunction=(uintptr_t)getter;DomChat::g_ChatGetterFunction=getterFunction;
 *(uint32_t*)(getter+0xB4)=(8u<<16)|1u;getterResult=chat;
 *(uintptr_t*)(controller+0x1420)=0;
 assert(DomChat::Broadcast("getter",error) && invoked==3);
 getterResult=bad;assert(!DomChat::Broadcast("wrong getter",error) && invoked==3);assert(error.find("BuildInteractionComponent")!=std::string::npos);
 getterResult=chat;
 // Stale/missing identity must refuse, never silently send zeros.
 *(int32_t*)(state+DomEngine::Offsets::PlayerState_PlayerId)=43;
 assert(!DomChat::Broadcast("stale identity",error) && invoked==3 && error.find("identity")!=std::string::npos);
 *(int32_t*)(state+DomEngine::Offsets::PlayerState_PlayerId)=42;
 stateGuid=std::string(32,'B');assert(!DomChat::Broadcast("changed GUID",error) && invoked==3 && error.find("identity")!=std::string::npos);stateGuid=p.characterGuid;
 unreadableBegin=(uintptr_t)state+DomEngine::Offsets::DomPlayerState_NetIdSecondary;unreadableEnd=unreadableBegin+0x30;
 assert(!DomChat::Broadcast("unreadable identity",error) && invoked==3 && error.find("identity")!=std::string::npos);unreadableBegin=unreadableEnd=0;
 getterResult=bad;
 // Incoming chat proves the real instance and continues relaying unchanged.
 frameReady=true;alignas(uintptr_t) unsigned char frame[0x100]{},params[0x88]{};
 std::u16string body=u"relay still works";DomChat::WriteFString((uintptr_t)params+0x78,body);*(int32_t*)(params+0x70)=42;*(uintptr_t*)(frame+0x28)=(uintptr_t)params;
 DomChat::AddListener([](const DomChat::Message& message){assert(message.body=="relay still works");++relayed;});
 ((void(*)(void*,void*,void*))hook)(component,frame,nullptr);assert(relayed==1 && originalCalls==2);
 assert(DomChat::Broadcast("observed",error) && invoked==4); // Observed instance beats stale getter/offset.
 DomChat::g_ChatComponents.clear();DomChat::g_ChatGetterFunction=0;*(uintptr_t*)(controller+0x1420)=chat;*(uintptr_t*)(controller+0x1430)=(uintptr_t)other;
 assert(!DomChat::Broadcast("ambiguous",error) && error.find("ambiguous")!=std::string::npos && invoked==4);
 *(uintptr_t*)(controller+0x1430)=0;*(uintptr_t*)(component+0x20)=0;
 assert(!DomChat::Broadcast("unowned",error) && invoked==4);
 players.clear();
 const std::string id="76561190000000000";DomBans::Load();assert(DomBans::IsReady());std::string message;
 assert(DomBans::Ban(id,"reason with } and \"quotes\"",message));DomBans::Load();assert(DomBans::IsBanned(id));
 assert(DomBans::Unban(id,message));DomBans::Load();assert(!DomBans::IsBanned(id));
 std::filesystem::rename(path,std::string(path)+"-moved");assert(!DomBans::Ban(id,"test",message) && !DomBans::IsBanned(id));
 std::filesystem::rename(std::string(path)+"-moved",path);assert(DomBans::Ban(id,"test",message));
 std::filesystem::rename(path,std::string(path)+"-moved");assert(!DomBans::Unban(id,message) && DomBans::IsBanned(id));
 // Commands use the native admin check, quote-aware arguments and one audit per attempt.
 DomEngine::PlayerInfo sender;sender.controllerPtr=(uintptr_t)controller;sender.characterName="Admin Hero";sender.characterGuid=std::string(32,'A');
 DomChat::g_AdminFunction=0xF00D;DomChat::g_AdminLibrary=1;
 assert(!DomChat::HandleCommand(sender,"ordinary chat"));assert(audits.empty());
 assert(DomChat::HandleCommand(sender,"/kick \"Two Word Name\" reason"));assert(audits.size()==1 && !audits.back().success);
 allowAdmin=true;
 assert(DomChat::HandleCommand(sender,"/kick \"Two Word Name\" \"Quoted reason\""));
 assert(kickedTarget=="Two Word Name" && kickedReason=="Quoted reason" && audits.size()==2 && !audits.back().success);
 assert(DomChat::HandleCommand(sender,"/give 'Two Word Name' 'Iron Bar' 25"));
 assert(giveTarget=="Two Word Name" && giveItem=="Iron Bar" && giveCount==25 && audits.back().success);
 assert(audits.back().result.find("Admin Hero")!=std::string::npos);
 auto previous=giveCount;assert(DomChat::HandleCommand(sender,"/give Hero Iron 2oops"));assert(giveCount==previous && !audits.back().success);
 assert(DomChat::HandleCommand(sender,"/give Hero Iron 100001"));assert(giveCount==previous && !audits.back().success);
 assert(DomChat::HandleCommand(sender,"/kick \"unclosed"));assert(!audits.back().success);
 assert(DomChat::HandleCommand(sender,"/help"));assert(audits.back().success);
 assert(DomChat::HandleCommand(sender,"/unban"));assert(!audits.back().success);
 std::cout<<"PASS: game-native admin gate, quoted command names/items, strict quantities and one audit per success/denial/usage/parse failure\n";
 std::filesystem::remove_all(std::string(path)+"-moved");
 std::cout<<"PASS: component-specific dispatch, saved observer forwarding, missing/non-image dispatch refusal, moved/wrong chat component, type/owner checks, getter validation, observed chat binding, unchanged relay, recipient identity fidelity, stale/missing identity refusal, system-tag exclusion, ambiguity refusal, Unicode and ban persistence\n";
}
