#include "service.h"
#include "recovery.h"
#include "http_audit.h"
#include "../engine/process_event.h"
#include "notification_style.h"
#include "combat.h"
#include "death_telemetry.h"
#include "log_damage.h"
#include "game_log_tail.h"
#include <filesystem>
#include "metrics.h"
#include "access.h"
#include "../engine/administration.h"
#include "../engine/world_save.h"
#include "../config/config.h"
#include "../discord/webhook.h"
#include "../discord/delivery.h"
#include "../engine/chat.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <memory>
namespace Thc {
namespace {
std::unique_ptr<Store>& Ledger(){static auto store=std::make_unique<Store>(GetBaseDir()+"/thc");return store;}
std::atomic<bool> started{false},worldReady{false},stopping{false},monitorError{false};
std::mutex chatMutex;struct Line{std::string guid,name,body;};std::deque<Line> lines;
std::atomic<uint64_t> chatDropped{0};
std::mutex logDamageMutex;
Json logDamageStatus={{"available",false},{"parsed",0},{"settled",0},{"rejected",0}};
GameLogTail& CombatLog(){
    static GameLogTail tail([]{
        std::error_code ec;auto exe=std::filesystem::read_symlink("/proc/self/exe",ec);
        return ec?std::string{}:(exe.parent_path().parent_path().parent_path()/"Saved/Logs/RSDragonwilds.log").string();
    }());return tail;
}
std::vector<Hit> LogHits(const std::vector<DomEngine::PlayerInfo>& players,double now){
    static LogDamage::Witness witness;static uint64_t generation=0;
    auto& tail=CombatLog();auto entries=tail.Read();
    if(generation!=tail.generation){witness.Reset();generation=tail.generation;}
    std::vector<LogDamage::Sample> samples;auto* engine=DomEngine::g_Engine;
    if(engine)for(const auto& p:players)if(p.hasHealth && p.pawnPtr && !p.characterGuid.empty() && !p.characterName.empty())
        samples.push_back({engine->GetObjectName(p.pawnPtr),p.characterGuid,p.pawnPtr,Mem::ReadI32(p.pawnPtr+DomEngine::Offsets::UObject_Index,-1),p.health,p.isDead});
    double wall=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto hits=witness.Observe(samples,entries,wall,now,tail.available && tail.complete);
    std::lock_guard<std::mutex> lock(logDamageMutex);
    logDamageStatus={{"available",tail.available},{"caughtUp",tail.complete},{"parsed",witness.parsed},{"settled",witness.settled},{"rejected",witness.rejected},{"generation",generation},
        {"coverage","Engine combat-log predictions correlated with exact character pawn binding and observed lethal health loss; missing or ambiguous causes remain unresolved"}};
    return hits;
}
Json LogDamageStatus(){std::lock_guard<std::mutex> lock(logDamageMutex);return logDamageStatus;}

void SendEvents(const std::vector<Json>& events) {
    for(const auto& event:events) {
        const auto type=event["type"].get<std::string>();const auto name=event["name"].get<std::string>();
        std::string text;Discord::Channel channel=Discord::Channel::Status;
        if(type=="join" || type=="leave") {channel=Discord::Channel::Presence;text=name+(type=="join"?" joined":" left");}
        else if(type=="death") {channel=Discord::Channel::Deaths;auto cause=event["details"].value("cause",std::string("unattributed"));text=name+" died — "+(cause=="unattributed"?std::string("cause not captured"):"**"+DiscordName(cause)+"**");}
        else if(type=="kill") {channel=Discord::Channel::Deaths;text=name+" credited with a kill: "+event["details"].value("victim",std::string("unknown"));}
        else if(type=="chat") {channel=Discord::Channel::Chat;text=name+": "+event["details"].value("message",std::string{});}
        else if(type=="admin") {channel=Discord::Channel::Admin;const auto& details=event["details"];text=details.contains("actor")?FormatHttpAudit(details):details.value("action",std::string{})+" "+details.value("target",std::string{})+": "+details.value("result",std::string{});}
        else text=event["details"].value("message",type);
        text=NotificationStyle(type,name,text);
        Discord::Post(channel,StyledNotification(type)?text:"[THC #"+std::to_string(event["id"].get<uint64_t>())+"] "+text);
    }
}
}
void Start(){
    if(started.exchange(true))return;
    WorldSave::Initialize(); // Construct before the startup thread registers shutdown.
    Access::Get().Load();
    if(!Ledger()->Load())LogMessage("THC: ledger unavailable; original state preserved");
    if(g_Config.discord.enabled){
        const auto& c=g_Config.discord;
        auto start=[&](Discord::Channel lane,const std::string& url,const std::string& thread){
            auto target=Discord::RouteWebhook(url,thread);
            if(!url.empty() && target.empty())LogMessage("Discord: invalid webhook/thread route; lane left disabled");
            Discord::Start(lane,target,c.username);
        };
        start(Discord::Channel::Chat,c.webhookUrl,c.chatThreadId);
        start(Discord::Channel::Deaths,c.deathWebhookUrl.empty()?c.webhookUrl:c.deathWebhookUrl,c.deathThreadId.empty()&&c.deathWebhookUrl.empty()?c.chatThreadId:c.deathThreadId);
        start(Discord::Channel::Presence,c.presenceWebhookUrl,c.presenceThreadId);
        start(Discord::Channel::Status,c.statusWebhookUrl,c.statusThreadId);
        start(Discord::Channel::Admin,c.adminWebhookUrl,c.adminThreadId);
    }
    SendEvents(Ledger()->Record("starting","","",time(nullptr),{{"message","THC loaded; waiting for a valid game world"}}));
    DomChat::AddListener([](const DomChat::Message& m){QueueChat(m.characterGuid,m.senderName,m.body);});
}
void QueueChat(const std::string& guid,const std::string& name,const std::string& body){
    if(!started || stopping || name.empty() || body.empty())return;
    std::unique_lock<std::mutex> lock(chatMutex,std::try_to_lock);
    if(!lock.owns_lock() || lines.size()>=512){++chatDropped;return;}
    lines.push_back({guid,name,body.substr(0,8000)});
}
void PollOnce(){
    if(!started || stopping)return;
    auto* engine=DomEngine::g_Engine;if(!engine || !engine->IsInitialized())return;
    auto state=engine->GetGameState();bool valid=state && engine->IsA(state,"DominionGameStateBase");
    if(valid) {
        FUEArray array{};
        valid=Mem::Read(state+DomEngine::Offsets::GameStateBase_PlayerArray,array) && array.Num>=0 && array.Num<=512 && array.Max>=array.Num;
        if(valid && array.Num>0) {
            valid=Mem::Readable(array.Data,sizeof(uintptr_t)*(size_t)array.Num);
            if(valid)for(int i=0;i<array.Num;++i) {
                auto p=Mem::ReadPtr((uintptr_t)array.Data+sizeof(uintptr_t)*(size_t)i);
                if(!p || !Mem::Readable((void*)p,0x370)){valid=false;break;}
            }
        }
    }
    bool was=worldReady.exchange(valid);
    if(valid!=was)SendEvents(Ledger()->Record(valid?"world_ready":"world_unavailable","","",time(nullptr),
        {{"message",valid?"Game world observed; THC monitoring active":"Game world unavailable; presence changes paused"}}));
    auto now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto players=valid?engine->GetAllPlayers():std::vector<DomEngine::PlayerInfo>{};
    bool requestedRecovery=Recovery().Take();
    try {
        static double retry=0;
        if(now>=retry || requestedRecovery){
            if(requestedRecovery && !GameThread::IsReady())GameThread::InstallExecPump();
            if(g_Config.combatTracking)Combat::Poll();
            Administration::Discover();WorldSave::Discover();
            if(requestedRecovery)DomChat::Initialize();
            retry=now+30;
        }
    }catch(...){if(requestedRecovery)Recovery().Finish(time(nullptr),false);throw;}
    auto saved=WorldSave::TakeCompletion();if(!saved.empty())SendEvents(Ledger()->Record(saved["success"].get<bool>()?"save":"save_failed","","",time(nullptr),{{"message",saved["success"].get<bool>()?"Game reported world save complete":"Game reported world-save failure"},{"slot",saved["slot"]}}));
    try{if(g_Config.combatTracking)DeathTelemetry::Update(players,Combat::ReflectionLayout());}
    catch(...){if(requestedRecovery)Recovery().Finish(time(nullptr),false);throw;}
    if(requestedRecovery)Recovery().Finish(time(nullptr),true);
    auto hits=valid?Combat::Drain(players,now):std::vector<Hit>{};
    auto deaths=DeathTelemetry::Drain();hits.insert(hits.end(),deaths.begin(),deaths.end());
    if(g_Config.combatTracking){
        auto logged=LogHits(players,now);
        for(auto& hit:logged){
            bool nativeSettled=false;
            for(const auto& native:hits)if(native.victimGuid==hit.victimGuid && native.fatalConfirmed){nativeSettled=true;break;}
            if(!nativeSettled)hits.push_back(std::move(hit));
        }
    }

    auto eventNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    SendEvents(Ledger()->Tick(players,valid,time(nullptr),eventNow,hits,3.0,DeathTelemetry::Bindings()));
    std::deque<Line> ready;{std::lock_guard<std::mutex> lock(chatMutex);ready.swap(lines);}
    for(const auto& line:ready)SendEvents(Ledger()->Record("chat",line.guid,line.name,time(nullptr),{{"message",line.body}}));
}
void Poll(){try{PollOnce();monitorError=false;}catch(...){monitorError=true;}}
bool AuditRequest(const Json& details){
 try{if(started && !stopping){auto events=Ledger()->Record("admin","","",time(nullptr),details);if(events.empty())return false;return Discord::TryPost(Discord::Channel::Admin,NotificationStyle("admin","",FormatHttpAudit(details)));}}
 catch(...){monitorError=true;}return false;
}
void Audit(const std::string& action,const std::string& target,const std::string& result,bool success){
    try {
        if(started && !stopping)SendEvents(Ledger()->Record("admin","","",time(nullptr),{{"action",action},{"target",target},{"result",result},{"success",success}}));
    }catch(...){monitorError=true;}
}
void Stop(){
    if(!started || stopping.exchange(true))return;
    WorldSave::Stop();
    Combat::Stop();
    DeathTelemetry::Stop();
    SendEvents(Ledger()->Record("shutdown","","",time(nullptr),{{"message","THC observed clean process shutdown"}}));
}
Json Data(){return Ledger()->Read();}
Json RequestRecovery(){return Recovery().Request(time(nullptr));}
Json ServiceStatus(){auto admin=Administration::Capabilities();admin["saveWorldNative"]=WorldSave::Available();admin["saveWorld"]=admin["saveWorld"].get<bool>()||WorldSave::Available();return {{"compatibility",{{"recovery",Recovery().Status()},{"combatTrackingEnabled",g_Config.combatTracking},{"gameThread",{{"pumpReady",GameThread::IsReady()},{"processEventFound",GameThread::HasProcessEvent()},{"frameLayoutValidated",GameThread::FrameLayoutReady()},{"pumpAgeSeconds",GameThread::PumpAgeSeconds()}}}}},{"started",started.load()},{"monitorError",monitorError.load()},{"worldObserved",worldReady.load()},{"ledger",Ledger()->Status()},
    {"administration",admin},{"worldSave",WorldSave::Status()},{"system",SystemMetrics(GetBaseDir())},{"combat",Combat::Status()},{"combatLog",LogDamageStatus()},{"deathTelemetry",DeathTelemetry::Status()},{"chatDropped",chatDropped.load()},{"discord",Json::parse(Discord::StatusJson())}};}
}
