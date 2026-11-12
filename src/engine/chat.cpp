#include "chat.h"
#include "bans.h"
#include "dom_engine.h"
#include "items.h"
#include "native_call.h"
#include "process_event.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "../utils/command_args.h"
#include "../thc/service.h"

#include <algorithm>
#include <atomic>
#include <codecvt>
#include <locale>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

namespace DomChat {

namespace {

using namespace DomEngine;

// FChatMessageData - Source: RSDWSDK/CppSDK/Assertions.inl
constexpr uintptr_t kSenderId      = 0x00;
constexpr uintptr_t kSecondaryId   = 0x30;
constexpr uintptr_t kCharacterGuid = 0x60;
constexpr uintptr_t kPlayerId      = 0x70;
constexpr uintptr_t kColor         = 0x74;
constexpr uintptr_t kMessageBody   = 0x78;
constexpr size_t    kMessageSize   = 0x88;

// FChatPlayerEventData - Source: RSDWSDK/CppSDK/SDK/JagexChatBackend_structs.hpp
constexpr uintptr_t kEventTag  = 0x78;
constexpr size_t    kEventSize = 0x80;

// ADominionPlayerController::PlayerChatComponent - Source: Assertions.inl
constexpr uintptr_t kControllerChatComponent = 0x1288;

typedef void (*ExecThunk)(void* context, void* frame, void* result);

constexpr uintptr_t kExecFunction = 0xD8;

ExecThunk g_OriginalSend = nullptr;
std::atomic<uintptr_t> g_SendFunction{0};
std::atomic<uintptr_t> g_ReceiveFunction{0};
std::atomic<uintptr_t> g_AdminFunction{0};
std::atomic<uintptr_t> g_AdminLibrary{0};
std::atomic<uintptr_t> g_ClientEventFunction{0};
std::atomic<uintptr_t> g_ServerEventFunction{0};
std::atomic<uintptr_t> g_SystemMessageFunction{0};
std::atomic<uintptr_t> g_ChatGetterFunction{0};
std::atomic<bool> g_Hooked{false};
std::mutex g_InitMutex;
std::string g_Status = "not initialised";

std::mutex g_ListenerMutex;
std::vector<Listener> g_Listeners;
std::vector<EventListener> g_EventListeners;

std::mutex g_VTableMutex;
std::map<uintptr_t, GameThread::ProcessEventFn> g_HookedVTables;
std::map<std::string, std::chrono::steady_clock::time_point> g_RecentEvents;

void WriteFString(uintptr_t dest, const std::u16string& value) {
    // ProcessEvent consumes the parameter block synchronously. The caller owns
    // the UTF-16 storage through the call; no per-message allocation is leaked.
    *(char16_t**)(dest) = const_cast<char16_t*>(value.c_str());
    *(int32_t*)(dest + 8) = (int32_t)value.size() + 1;
    *(int32_t*)(dest + 12) = (int32_t)value.size() + 1;
}

void Deliver(const Message& message) {
    std::vector<Listener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_ListenerMutex);
        listeners = g_Listeners;
    }
    for (const Listener& listener : listeners) {
        try {
            listener(message);
        } catch (...) {
        }
    }
}

void DeliverEvent(const PlayerEvent& event) {
    std::vector<EventListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_ListenerMutex);
        listeners = g_EventListeners;
    }
    for (const EventListener& listener : listeners) {
        try {
            listener(event);
        } catch (...) {
        }
    }
}

void HandlePlayerEvent(uintptr_t params) {
    Engine* engine = g_Engine;
    if (!engine || !Mem::Readable((void*)params, kEventSize)) return;

    PlayerEvent event;
    uint32_t tagIndex = (uint32_t)Mem::ReadI32(params + kEventTag);
    uint32_t tagNumber = (uint32_t)Mem::ReadI32(params + kEventTag + 4);
    event.tag = engine->NameToString(tagIndex, tagNumber);
    if (event.tag.empty() || event.tag == "None") return;

    event.characterGuid = engine->ReadGuid(params + kCharacterGuid);
    event.playerId = Mem::ReadI32(params + kPlayerId);
    std::string source;
    event.senderNetId = engine->ReadUniqueNetId(params + kSenderId, source);

    std::string key = std::to_string(event.playerId) + "|" + event.characterGuid + "|" + event.tag;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_VTableMutex);
        auto it = g_RecentEvents.find(key);
        if (it != g_RecentEvents.end() && now - it->second < std::chrono::seconds(3)) return;
        g_RecentEvents[key] = now;
        for (auto entry = g_RecentEvents.begin(); entry != g_RecentEvents.end();) {
            if (now - entry->second > std::chrono::seconds(30)) entry = g_RecentEvents.erase(entry);
            else ++entry;
        }
    }

    for (const PlayerInfo& player : engine->GetAllPlayers()) {
        if (player.playerId != event.playerId &&
            (event.characterGuid.empty() || player.characterGuid != event.characterGuid)) continue;
        event.senderName = player.characterName.empty() ? player.name : player.characterName;
        if (event.senderNetId.empty()) event.senderNetId = player.uniqueNetId;
        break;
    }

    LogMessage("Chat: player event '" + event.tag + "' from " +
               (event.senderName.empty() ? "playerId " + std::to_string(event.playerId) : event.senderName));
    DeliverEvent(event);
}

bool MessageFunctionReady(uintptr_t fn);

void HookedChatProcessEvent(void* self, void* function, void* params) {
    GameThread::ProcessEventFn previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_VTableMutex);
        uintptr_t vtable = Mem::ReadPtr((uintptr_t)self);
        auto it = g_HookedVTables.find(vtable);
        if (it != g_HookedVTables.end()) previous = it->second;
    }

    uintptr_t fn = (uintptr_t)function;
    if (fn && (fn == g_ClientEventFunction || fn == g_ServerEventFunction)) {
        HandlePlayerEvent((uintptr_t)params);
    } else if (fn && fn == g_SystemMessageFunction && !MessageFunctionReady(fn) && g_Engine && Mem::Readable(params, 8)) {
        uint32_t tagIndex = (uint32_t)Mem::ReadI32((uintptr_t)params);
        uint32_t tagNumber = (uint32_t)Mem::ReadI32((uintptr_t)params + 4);
        LogMessage("Chat: system message '" + g_Engine->NameToString(tagIndex, tagNumber) + "'");
    }

    if (previous) previous(self, function, params);
}

bool MessageFunctionReady(uintptr_t fn) {
    if(!fn)return false;
    uint32_t flags=(uint32_t)Mem::ReadI32(fn+0xB0);
    uint32_t shape=(uint32_t)Mem::ReadI32(fn+0xB4);
    return (flags&0x01000440u)==0x01000440u && ((shape>>16)&0xffff)==kMessageSize && (shape&0xff)==1;
}

uintptr_t BroadcastFunction() {
    // Only this text RPC has a verified message contract. The live Linux
    // Client_ReceiveSystemMessage takes an 8-byte notification tag, not text.
    auto receive=g_ReceiveFunction.load();
    return MessageFunctionReady(receive)?receive:0;
}

struct ComponentRecord {uintptr_t component=0,playerState=0;int32_t index=-1;};
std::mutex g_ComponentMutex;
std::map<uintptr_t,ComponentRecord> g_ChatComponents;
std::atomic<uint64_t> g_ComponentRejected{0};

bool IsChatComponent(uintptr_t component,uintptr_t controller) {
    auto* engine=g_Engine;
    return engine && component && controller && Mem::Readable((void*)component,0x28) &&
           engine->IsA(component,"PlayerChatComponent") &&
           Mem::ReadPtr(component+Offsets::UObject_Outer)==controller;
}

void RememberChatComponent(const PlayerInfo& player,uintptr_t component) {
    if(!IsChatComponent(component,player.controllerPtr))return;
    ComponentRecord record{component,player.playerStatePtr,Mem::ReadI32(component+Offsets::UObject_Index,-1)};
    std::lock_guard<std::mutex> lock(g_ComponentMutex);
    if(g_ChatComponents.size()>=512 && !g_ChatComponents.count(player.controllerPtr))g_ChatComponents.clear();
    g_ChatComponents[player.controllerPtr]=record;
}

uintptr_t ResolveChatComponent(const PlayerInfo& player,bool allowGetter,
                               std::string& source,std::string& reason) {
    auto* engine=g_Engine;
    if(!engine || !player.controllerPtr){reason="player controller unavailable";return 0;}
    ComponentRecord observed;
    {
        std::lock_guard<std::mutex> lock(g_ComponentMutex);
        auto it=g_ChatComponents.find(player.controllerPtr);
        if(it!=g_ChatComponents.end())observed=it->second;
    }
    if(observed.playerState==player.playerStatePtr && IsChatComponent(observed.component,player.controllerPtr) &&
       observed.index==Mem::ReadI32(observed.component+Offsets::UObject_Index,-1)) {
        source="observed-chat";return observed.component;
    }
    auto getter=g_ChatGetterFunction.load();
    if(allowGetter && getter && GameThread::HasProcessEvent()) {
        uint32_t shape=(uint32_t)Mem::ReadI32(getter+0xB4);
        uint32_t flags=(uint32_t)Mem::ReadI32(getter+0xB0);
        if(((shape>>16)&0xffff)==sizeof(uintptr_t) && (shape&0xff)==1 && !(flags&0x40u)) {
            uintptr_t component=0;
            GameThread::CallFunction(player.controllerPtr,getter,&component);
            if(IsChatComponent(component,player.controllerPtr)){source="game-getter";return component;}
            reason="game chat getter returned "+(component?engine->GetObjectClassName(component):"null")+"; refusing invalid chat target";
            ++g_ComponentRejected;return 0;
        }
    }
    auto candidate=Mem::ReadPtr(player.controllerPtr+kControllerChatComponent);
    if(IsChatComponent(candidate,player.controllerPtr)){source="validated-offset";return candidate;}
    // The live log proved +0x1288 now names BuildInteractionComponent. Treat
    // it as a hint only, then search pointer fields for one owned chat instance.
    uintptr_t found=0;
    for(uintptr_t offset=0x300;offset<0x1800;offset+=sizeof(uintptr_t)) {
        auto component=Mem::ReadPtr(player.controllerPtr+offset);
        if(!IsChatComponent(component,player.controllerPtr))continue;
        if(found && found!=component){reason="multiple owned PlayerChatComponents; refusing ambiguous target";++g_ComponentRejected;return 0;}
        found=component;
    }
    if(found){source="controller-scan";return found;}
    reason="no owned PlayerChatComponent resolved";
    if(candidate)reason+="; old offset points to "+engine->GetObjectClassName(candidate);
    ++g_ComponentRejected;return 0;
}

struct InvokeRecord {std::string source,reason,path;uintptr_t dispatch=0;};

// The pump discovers ProcessEvent using PlayerController objects. Its shared
// implementation is not necessarily the component's override. Preserve the
// receiver's own dispatch, including the original saved before our observer.
GameThread::ProcessEventFn ComponentDispatch(uintptr_t component) {
    const int slot=GameThread::VTableIndex();
    if(slot<0 || slot>=200)return nullptr;
    const uintptr_t vtable=Mem::ReadPtr(component);
    if(!vtable || !Mem::InImage((void*)vtable))return nullptr;
    GameThread::ProcessEventFn target=nullptr;
    {
        std::lock_guard<std::mutex> lock(g_VTableMutex);
        auto it=g_HookedVTables.find(vtable);
        if(it!=g_HookedVTables.end())target=it->second;
        else target=(GameThread::ProcessEventFn)Mem::ReadPtr(vtable+(uintptr_t)slot*sizeof(uintptr_t));
    }
    if(!target || target==&HookedChatProcessEvent || !Mem::InImage((void*)target))return nullptr;
    return target;
}
bool SendToPlayer(const PlayerInfo& player,const std::string& body,InvokeRecord* report=nullptr) {
    InvokeRecord local;
    auto& record=report?*report:local;
    auto function=BroadcastFunction();
    if(!function || !GameThread::HasProcessEvent()){record.reason="no validated client-message RPC";return false;}
    auto component=ResolveChatComponent(player,true,record.source,record.reason);
    if(!component)return false;
    record.path="chat-receive";
    alignas(uintptr_t) uint8_t params[kMessageSize]{};
    // Preserve RSDW's recipient identity contract. Blank sender fields were an
    // unverified assumption and failed client testing. The Linux report in
    // dkoz/RSDWServerAPI#6 describes this original marshal displaying correctly
    // after fixing component discovery, albeit under the recipient's name.
    const auto state=player.playerStatePtr;
    if(!state || !Mem::Readable((void*)(state+Offsets::PlayerState_UniqueID),Offsets::NetIdRepl_Size) ||
       !Mem::Readable((void*)(state+Offsets::DomPlayerState_NetIdSecondary),Offsets::NetIdRepl_Size) ||
       !Mem::Readable((void*)(state+Offsets::DomPlayerState_CharacterGuid),16) ||
       Mem::ReadI32(state+Offsets::PlayerState_PlayerId,-1)!=player.playerId ||
       (!player.characterGuid.empty() && g_Engine->ReadGuid(state+Offsets::DomPlayerState_CharacterGuid)!=player.characterGuid)) {
        record.reason="recipient identity unavailable or changed; no message invoked";return false;
    }
    memcpy(params+kSenderId,(void*)(state+Offsets::PlayerState_UniqueID),Offsets::NetIdRepl_Size);
    memcpy(params+kSecondaryId,(void*)(state+Offsets::DomPlayerState_NetIdSecondary),Offsets::NetIdRepl_Size);
    memcpy(params+kCharacterGuid,(void*)(state+Offsets::DomPlayerState_CharacterGuid),16);
    memcpy(params+kPlayerId,&player.playerId,sizeof(player.playerId));
    *(uint32_t*)(params+kColor)=0xFFFFFFFF;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> converter;
    std::u16string text=converter.from_bytes(body);
    if(text.size()>1024){record.reason="message exceeds 1024 UTF-16 units";return false;}
    WriteFString((uintptr_t)params+kMessageBody,text);
    auto dispatch=ComponentDispatch(component);
    if(!dispatch){record.reason="validated component ProcessEvent dispatch unavailable";return false;}
    record.dispatch=(uintptr_t)dispatch;
    dispatch((void*)component,(void*)function,params);
    return true;
}

bool IsAdmin(uintptr_t controller) {
    if (!controller || !g_AdminFunction || !g_AdminLibrary) return false;
    if (!NativeCall::IsNative(g_AdminFunction)) return false;

    struct Params {
        uintptr_t Controller;
        bool ReturnValue;
        uint8_t Pad[7];
    } params = {};
    params.Controller = controller;

    if (!NativeCall::Invoke(g_AdminLibrary, g_AdminFunction, &params, 0x08)) return false;
    return params.ReturnValue;
}

std::string Join(const std::vector<std::string>& parts, size_t from) {
    std::string out;
    for (size_t i = from; i < parts.size(); i++) {
        if (!out.empty()) out += " ";
        out += parts[i];
    }
    return out;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return value;
}

bool HandleCommand(const PlayerInfo& sender, const std::string& body) {
    std::istringstream first(body);std::string verb;first>>verb;
    const std::string command=Lower(verb);
    if(command!="/kick" && command!="/ban" && command!="/unban" && command!="/give" && command!="/help")return false;
    std::vector<std::string> parts;std::string parseError;
    bool parsed=CommandArgs::Parse(body,parts,parseError);
    const auto actor=sender.characterName.empty()?sender.name:sender.characterName;
    auto reply=[&](const std::string& result,bool success=false){
        auto target=parsed && parts.size()>1?parts[1]:std::string{};
        Thc::Audit("in-game:"+command,target,"Actor: "+actor+" [character "+sender.characterGuid+"] — "+result,success);
        LogMessage("Chat command: "+actor+" "+command+" -> "+result);
        SendToPlayer(sender,"[Server] "+result);
    };
    if(!IsAdmin(sender.controllerPtr)){reply("You are not an admin.");return true;}
    if(!parsed){reply(parseError);return true;}
    if(command=="/help"){
        SendToPlayer(sender,"[Server] /kick <player> [reason] | /ban <player> [reason]");
        SendToPlayer(sender,"[Server] /unban <player or SteamID64> | /give <player> <item> [count]");
        reply("Put two-word names in quotes, for example /kick \"Two Word Name\". Give count: 1..100000.",true);
        return true;
    }
    auto* engine=g_Engine;
    if(!engine){reply("Engine is not initialised.");return true;}
    if(command=="/kick"){
        if(parts.size()<2){reply("Usage: /kick <player> [reason]");return true;}
        std::string reason=parts.size()>2?Join(parts,2):"Kicked by an administrator",error;
        bool ok=engine->KickPlayer(parts[1],reason,error);
        reply(ok?"Kicked "+parts[1]:"Kick failed: "+error,ok);
    }else if(command=="/ban"){
        if(parts.size()<2){reply("Usage: /ban <player> [reason]");return true;}
        std::string message,reason=parts.size()>2?Join(parts,2):"Banned by an administrator";
        bool ok=DomBans::Ban(parts[1],reason,message);reply(message,ok);
    }else if(command=="/unban"){
        if(parts.size()!=2){reply("Usage: /unban <player or SteamID64>; quote names containing spaces");return true;}
        std::string message;bool ok=DomBans::Unban(parts[1],message);reply(message,ok);
    }else if(command=="/give"){
        if(parts.size()<3 || parts.size()>4){reply("Usage: /give <player> <item> [count]; quote names containing spaces");return true;}
        int count=1;
        if(parts.size()==4 && !CommandArgs::Count(parts[3],count)){reply("Count must be a whole number between 1 and 100000.");return true;}
        std::string message;bool ok=DomItems::GiveItem(parts[1],parts[2],count,message);reply(message,ok);
    }
    return true;
}

void SendThunk(void* context, void* frame, void* result) {
    Engine* engine = g_Engine;
    bool suppress = false;

    if (engine && engine->IsInitialized() && GameThread::FrameLayoutReady()) {
        uintptr_t locals = Mem::ReadPtr((uintptr_t)frame + GameThread::FrameLocalsOffset());
        if (locals && Mem::Readable((void*)locals, kMessageSize)) {
            Message message;
            message.body = engine->ReadFString(locals + kMessageBody);
            if (!message.body.empty()) {
                message.characterGuid = engine->ReadGuid(locals + kCharacterGuid);
                message.playerId = Mem::ReadI32(locals + kPlayerId);

                std::string source;
                message.senderNetId = engine->ReadUniqueNetId(locals + kSenderId, source);

                PlayerInfo sender;
                bool haveSender = false;
                for (const PlayerInfo& player : engine->GetAllPlayers()) {
                    if (player.playerId != message.playerId) continue;
                    sender = player;
                    haveSender = true;
                    message.senderName =
                        player.characterName.empty() ? player.name : player.characterName;
                    if (message.senderNetId.empty()) message.senderNetId = player.uniqueNetId;
                    break;
                }

                if(haveSender)RememberChatComponent(sender,(uintptr_t)context);
                if (haveSender && !message.body.empty() && message.body[0] == '/') {
                    suppress = HandleCommand(sender, message.body);
                }

                if (!suppress) Deliver(message);
            }
        }
    }

    if (!suppress && g_OriginalSend) g_OriginalSend(context, frame, result);
}

}

bool IsHooked() { return g_Hooked; }
std::string Status() {
    std::lock_guard<std::mutex> lock(g_InitMutex);
    return g_Status;
}

bool CanBroadcast() {
    return BroadcastFunction() && GameThread::IsReady() && GameThread::HasProcessEvent();
}

void AddListener(Listener listener) {
    std::lock_guard<std::mutex> lock(g_ListenerMutex);
    g_Listeners.push_back(listener);
}

void AddEventListener(EventListener listener) {
    std::lock_guard<std::mutex> lock(g_ListenerMutex);
    g_EventListeners.push_back(listener);
}

void Poll() {
    // Retry on the background poll, not in a network request or a game callback.
    static auto retryAt = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (now >= retryAt) {
        Initialize();
        retryAt = now + std::chrono::seconds(10);
    }
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized() || !g_Hooked) return;
    if (!g_ClientEventFunction && !g_ServerEventFunction && !g_SystemMessageFunction) return;
    if (GameThread::VTableIndex() < 0) return;

    for (const PlayerInfo& player : engine->GetAllPlayers()) {
        if (!player.controllerPtr) continue;
        std::string source,reason;
        uintptr_t component=ResolveChatComponent(player,false,source,reason);
        if(!component)continue;
        uintptr_t vtable = Mem::ReadPtr(component);
        if (!vtable) continue;

        {
            std::lock_guard<std::mutex> lock(g_VTableMutex);
            if (g_HookedVTables.count(vtable)) continue;
        }

        GameThread::ProcessEventFn previous = nullptr;
        if (!GameThread::HookVTable(component, &HookedChatProcessEvent, previous)) continue;
        if (!previous) continue;

        {
            std::lock_guard<std::mutex> lock(g_VTableMutex);
            g_HookedVTables[vtable] = previous;
        }
        LogMessage("Chat: watching player events on " + engine->GetObjectClassName(component));
    }
}

bool Initialize() {
    std::lock_guard<std::mutex> lock(g_InitMutex);
    auto setStatus = [](const std::string& status) {
        if (g_Status != status) LogMessage("Chat: " + status);
        g_Status = status;
    };

    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) {
        g_Status = "engine not initialised";
        return false;
    }

    if (!g_SendFunction) g_SendFunction = engine->FindFunction("PlayerChatComponent", "Server_SendChatMessage");
    if (!g_ReceiveFunction) g_ReceiveFunction = engine->FindFunction("PlayerChatComponent", "Client_ReceiveChatMessage");
    if(!g_ChatGetterFunction)g_ChatGetterFunction=engine->FindFunction("DominionPlayerController","GetPlayerChatComponent");
    if(!g_ChatGetterFunction)g_ChatGetterFunction=engine->FindFunction("DominionPlayerControllerBase","GetPlayerChatComponent");
    if (!g_AdminFunction) g_AdminFunction = engine->FindFunction("PrivilegeFunctionLibrary", "ControllerIsOwnerOrAdmin");
    if (!g_AdminLibrary) g_AdminLibrary = engine->FindObject("PrivilegeFunctionLibrary", true);
    if (!g_ClientEventFunction) g_ClientEventFunction = engine->FindFunction("PlayerChatComponent", "Client_ReceivePlayerEvent");
    if (!g_ServerEventFunction) g_ServerEventFunction = engine->FindFunction("PlayerChatComponent", "Server_SendPlayerEvent");
    if (!g_SystemMessageFunction) g_SystemMessageFunction = engine->FindFunction("PlayerChatComponent", "Client_ReceiveSystemMessage");

    if (g_Hooked) {
        setStatus(BroadcastFunction() ? "hooked" : "hooked; waiting for a broadcast function");
        return true;
    }
    if (!g_SendFunction) {
        setStatus("waiting for Server_SendChatMessage (retrying every 10s)");
        return false;
    }

    uintptr_t original = Mem::ReadPtr(g_SendFunction + kExecFunction);
    if (!original || !Mem::InImage((void*)original)) {
        setStatus("Server_SendChatMessage has no usable thunk (retrying every 10s)");
        return false;
    }

    g_OriginalSend = (ExecThunk)original;
    *(uintptr_t*)(g_SendFunction.load() + kExecFunction) = (uintptr_t)&SendThunk;

    g_Hooked = true;
    setStatus(BroadcastFunction() ? "hooked" : "hooked; waiting for a broadcast function");
    LogMessage("Chat: reading messages from PlayerChatComponent.Server_SendChatMessage");
    if (!BroadcastFunction()) LogMessage("Chat: broadcast function unresolved; retries active");
    if (!g_AdminFunction || !g_AdminLibrary) LogMessage("Chat: ControllerIsOwnerOrAdmin not found, chat commands unavailable");
    return true;
}

bool Broadcast(const std::string& body, std::string& outError) {
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) {
        outError = "engine not initialised";
        return false;
    }
    if (!BroadcastFunction()) {
        outError = "Client_ReceiveChatMessage unavailable or its reflected text signature is unsupported";
        return false;
    }
    if (!GameThread::IsReady()) {
        outError = "broadcast needs the game thread pump: " + GameThread::Status();
        return false;
    }
    if (!GameThread::HasProcessEvent()) {
        outError = "broadcast needs ProcessEvent, which has not been located yet";
        return false;
    }
    if (body.empty()) {
        outError = "message is empty";
        return false;
    }

    std::vector<PlayerInfo> players = engine->GetAllPlayers();
    if (players.empty()) {
        outError = "no players to broadcast to";
        return false;
    }

    auto system=g_SystemMessageFunction.load(),receive=g_ReceiveFunction.load();
    LogMessage("Chat: broadcast function metadata receiveFlags="+HexString((uint32_t)Mem::ReadI32(receive?receive+0xB0:0))+
               " receiveBytes="+std::to_string(((uint32_t)Mem::ReadI32(receive?receive+0xB4:0)>>16)&0xffff)+
               " systemFlags="+HexString((uint32_t)Mem::ReadI32(system?system+0xB0:0))+
               " systemBytes="+std::to_string(((uint32_t)Mem::ReadI32(system?system+0xB4:0)>>16)&0xffff));
    int invoked=0;
    std::vector<InvokeRecord> records(players.size());
    bool ran=GameThread::RunSync([&](){
        for(size_t i=0;i<players.size();++i)if(SendToPlayer(players[i],body,&records[i]))++invoked;
    },10000);

    if (!ran) {
        outError = "the game thread did not run the broadcast within 10s";
        return false;
    }
    for(size_t i=0;i<records.size();++i){
        const auto& r=records[i];
        LogMessage("Chat: broadcast recipient "+std::to_string(players[i].playerId)+
                   " componentSource="+r.source+" path="+r.path+" senderIdentity=recipient"+
                   " componentDispatch="+HexString(r.dispatch)+
                   " sharedDispatch="+HexString(GameThread::ProcessEventAddress())+
                   (r.reason.empty()?" -> RPC invoked (client display unconfirmed)":" -> refused: "+r.reason));
    }
    if(!invoked){
        outError="no valid chat recipients";
        for(const auto& r:records)if(!r.reason.empty()){outError+=": "+r.reason;break;}
        return false;
    }
    LogMessage("Chat: broadcast RPC invoked for "+std::to_string(invoked)+" player(s); client display unconfirmed");
    return true;
}

}
