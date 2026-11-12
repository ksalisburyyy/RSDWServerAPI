#include "combat.h"
#include "creature_names.h"
#include "damage_fields.h"
#include "../engine/process_event.h"
#include "../utils/memory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <set>
#include <cctype>
namespace Thc::Combat {
namespace {
using Thunk=void(*)(void*,void*,void*);
struct Walk { uintptr_t name=0,next=0,offset=0; };
Walk walk;
std::atomic<bool> layoutReady{false},stopped{false};
struct Hook { uintptr_t function=0;Parameters params;Thunk original=nullptr;std::atomic<bool> active{false}; };
std::array<Hook,2> hooks;
Hook predictHook;std::array<Hook,2> healthHooks;
DamageFields::Layout typedLayout;
struct Raw {uintptr_t victim=0,instigator=0,causer=0,type=0;int index=0,instigatorIndex=-1,causerIndex=-1,typeIndex=-1;double at=0;float damage=0,before=-1,after=-1;bool predicted=false,healthWitness=false,ambiguous=false;std::string explicitCause;};
std::mutex queueMutex;
std::deque<Raw> queue;
std::deque<Raw> predictions;
std::atomic<uint64_t> captured{0},dropped{0},unresolved{0},healthMissing{0};
std::atomic<int> armed{0};
std::atomic<bool> typedReady{false};
std::string status="waiting for reflected damage functions";
std::mutex statusMutex;
Json predictionDiagnostic={{"state","not inspected"}};
std::atomic<uint64_t> predictCalls{0},healthCalls{0};
std::atomic<uint64_t> predictCaptured{0},predictExpired{0},healthCaptured{0},settledFatal{0},unresolvedFatal{0};
float NpcHealth(uintptr_t actor);
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void SnapshotSources(Raw& raw){
    raw.instigatorIndex=Mem::ReadI32(raw.instigator+DomEngine::Offsets::UObject_Index,-1);
    raw.causerIndex=Mem::ReadI32(raw.causer+DomEngine::Offsets::UObject_Index,-1);
    raw.typeIndex=Mem::ReadI32(raw.type+DomEngine::Offsets::UObject_Index,-1);
}
void Enqueue(Raw raw){
    raw.at=Now();std::unique_lock<std::mutex> lock(queueMutex,std::try_to_lock);
    if(!lock.owns_lock() || queue.size()>=1024){++dropped;return;}
    try{queue.push_back(std::move(raw));++captured;}catch(...){++dropped;}
}
std::string EnvironmentalCause(std::string name){
    if(name.rfind("Default__",0)==0)name.erase(0,9);
    if(name.rfind("BP_",0)==0)name.erase(0,3);
    if(name.size()>2 && name.substr(name.size()-2)=="_C")name.resize(name.size()-2);
    std::string key;for(unsigned char c:name)if(std::isalnum(c))key+=(char)std::tolower(c);
    static const std::map<std::string,std::string> names={
        {"fall","Fall damage"},{"falling","Fall damage"},{"fallingdamage","Fall damage"},{"falldamage","Fall damage"},{"damagetypefall","Fall damage"},{"dtfall","Fall damage"},{"dafalldamage","Fall damage"},{"gefalldamage","Fall damage"},
        {"outofbounds","Out of bounds"},{"outofworld","Out of bounds"},{"felloutofworld","Out of bounds"},{"killz","Out of bounds"},{"killzdamage","Out of bounds"},{"damagetypekillz","Out of bounds"},
        {"killzvolume","Out of bounds"},{"suicide","Suicide"},{"playersuicide","Suicide"},{"damagetypesuicide","Suicide"},
        {"fire","Fire"},{"burning","Fire"},{"firedamage","Fire"},{"damagetypefire","Fire"},{"gefiredamage","Fire"},{"poison","Poison"},{"poisondamage","Poison"},{"damagetypepoison","Poison"},{"starvation","Starvation"},{"starvationdamage","Starvation"},{"thirst","Dehydration"},{"dehydration","Dehydration"},{"drowning","Drowning"},{"drowningdamage","Drowning"}
    };
    auto it=names.find(key);return it==names.end()?std::string{}:it->second;
}

bool WalkFields(uintptr_t fn,const Walk& candidate,Parameters& out) {
    auto* engine=DomEngine::g_Engine;if(!engine)return false;
    int size=(int)(((uint32_t)Mem::ReadI32(fn+0xB4)>>16)&0xffff);
    if(size<4 || size>2048)return false;
    uintptr_t field=Mem::ReadPtr(fn+0x50);std::set<uintptr_t> seen;out={};out.size=size;
    for(int i=0;field && i<16;++i) {
        if(!seen.insert(field).second || !Mem::Readable((void*)field,0x78))return false;
        auto name=engine->NameToString((uint32_t)Mem::ReadI32(field+candidate.name),(uint32_t)Mem::ReadI32(field+candidate.name+4));
        if(name.empty() || name=="None" || name.size()>64 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)return false;
        int offset=Mem::ReadI32(field+candidate.offset,-1);
        if(offset<0 || offset>=size || !out.fields.emplace(name,offset).second)return false;
        field=Mem::ReadPtr(field+candidate.next);
    }
    return field==0 && !out.fields.empty();
}
bool Validate(const Parameters& p) {
    std::vector<std::pair<int,int>> spans;
    for(auto entry:std::array<std::pair<const char*,int>,5>{{{"DamagedActor",8},{"BaseDamage",4},{"EventInstigator",8},{"DamageCauser",8},{"ReturnValue",4}}}) {
        auto it=p.fields.find(entry.first);if(it==p.fields.end() || it->second+entry.second>p.size)return false;
        if(entry.second==8 && it->second%8)return false;
        spans.push_back({it->second,it->second+entry.second});
    }
    std::sort(spans.begin(),spans.end());for(size_t i=1;i<spans.size();++i)if(spans[i].first<spans[i-1].second)return false;
    return true;
}
void Capture(size_t slot,void* context,void* frame,void* result) {
    auto& hook=hooks[slot]; Raw raw;bool valid=false;
    if(!stopped && hook.active && GameThread::FrameLayoutReady() && frame &&
       Mem::ReadPtr((uintptr_t)frame+GameThread::FrameNodeOffset())==hook.function) {
        uintptr_t locals=Mem::ReadPtr((uintptr_t)frame+GameThread::FrameLocalsOffset());
        if(locals && Mem::Readable((void*)locals,(size_t)hook.params.size)) {
            raw.victim=Mem::ReadPtr(locals+hook.params.fields.at("DamagedActor"));
            raw.instigator=Mem::ReadPtr(locals+hook.params.fields.at("EventInstigator"));
            raw.causer=Mem::ReadPtr(locals+hook.params.fields.at("DamageCauser"));
            auto type=hook.params.fields.find("DamageTypeClass");if(type!=hook.params.fields.end())raw.type=Mem::ReadPtr(locals+type->second);
            raw.index=Mem::ReadI32(raw.victim+DomEngine::Offsets::UObject_Index,-1);
            valid=raw.victim && raw.index>=0 && Mem::Readable((void*)raw.victim,0x28);
            if(valid){auto* engine=DomEngine::g_Engine;if(engine && engine->IsA(raw.victim,"DominionPlayerCharacter"))raw.before=NpcHealth(raw.victim);SnapshotSources(raw);}
        }
    }
    // Never swallow, repeat or change the real native call.
    if(hook.original)hook.original(context,frame,result);
    if(!valid || !result || !Mem::Read((uintptr_t)result,raw.damage) || !std::isfinite(raw.damage) || raw.damage<=0 || raw.damage>1000000)return;
    if(raw.before>=0)raw.after=NpcHealth(raw.victim);
    Enqueue(std::move(raw));
}
void Hook0(void* c,void* f,void* r){Capture(0,c,f,r);}
void Hook1(void* c,void* f,void* r){Capture(1,c,f,r);}
void PredictThunk(void* context,void* frame,void* result){
    ++predictCalls;
    Raw raw;bool valid=false;auto* engine=DomEngine::g_Engine;
    if(!stopped && predictHook.active && engine && GameThread::FrameLayoutReady() && frame && Mem::ReadPtr((uintptr_t)frame+GameThread::FrameNodeOffset())==predictHook.function && engine->IsA((uintptr_t)context,"DamageComponent")){
        auto locals=Mem::ReadPtr((uintptr_t)frame+GameThread::FrameLocalsOffset());
        raw.victim=Mem::ReadPtr((uintptr_t)context+DomEngine::Offsets::UObject_Outer);
        if(locals && Mem::Readable((void*)locals,736) && engine->IsA(raw.victim,"DominionPlayerCharacter")){
            raw.instigator=Mem::ReadPtr(locals+predictHook.params.fields.at("Instigator"));
            raw.causer=Mem::ReadPtr(locals+predictHook.params.fields.at("Source"));
            raw.index=Mem::ReadI32(raw.victim+DomEngine::Offsets::UObject_Index,-1);raw.before=NpcHealth(raw.victim);raw.predicted=true;
            valid=raw.index>=0 && raw.before>0;SnapshotSources(raw);
        }
    }
    if(predictHook.original)predictHook.original(context,frame,result);
    if(!valid || !result || !Mem::Read((uintptr_t)result,raw.damage) || !std::isfinite(raw.damage) || raw.damage<=0 || raw.damage>1000000)return;
    raw.after=NpcHealth(raw.victim);++predictCaptured;Enqueue(std::move(raw));
}
void HealthCapture(size_t slot,void* context,void* frame,void* result){
    ++healthCalls;
    auto& hook=healthHooks[slot];Raw raw;bool valid=false;auto* engine=DomEngine::g_Engine;
    if(!stopped && hook.active && engine && GameThread::FrameLayoutReady() && frame && Mem::ReadPtr((uintptr_t)frame+GameThread::FrameNodeOffset())==hook.function && engine->IsA((uintptr_t)context,"HealthComponent")){
        auto locals=Mem::ReadPtr((uintptr_t)frame+GameThread::FrameLocalsOffset());raw.victim=Mem::ReadPtr((uintptr_t)context+DomEngine::Offsets::UObject_Outer);
        if(locals && Mem::Readable((void*)locals,24) && engine->IsA(raw.victim,"DominionPlayerCharacter") && Mem::ReadPtr(raw.victim+DomEngine::Offsets::DomPlayerChar_HealthComponent)==(uintptr_t)context){
            auto address=locals+hook.params.fields.at("Context");FUEArray text{};
            if(Mem::Read(address,text) && text.Num>0 && text.Num<=256 && text.Max>=text.Num && Mem::Readable(text.Data,(size_t)text.Num*2))raw.explicitCause=EnvironmentalCause(engine->ReadFString(address));
            raw.before=NpcHealth(raw.victim);raw.index=Mem::ReadI32(raw.victim+DomEngine::Offsets::UObject_Index,-1);valid=raw.before>0 && raw.index>=0;
        }
    }
    if(hook.original)hook.original(context,frame,result);
    if(!valid)return;
    raw.after=NpcHealth(raw.victim);raw.damage=raw.before-raw.after;
    if(raw.after<0 || raw.damage<=0 || !std::isfinite(raw.damage))return;
    raw.healthWitness=true;++healthCaptured;Enqueue(std::move(raw));
}
void Health0(void* c,void* f,void* r){HealthCapture(0,c,f,r);}
void Health1(void* c,void* f,void* r){HealthCapture(1,c,f,r);}
float NpcHealth(uintptr_t actor) {
    auto* engine=DomEngine::g_Engine;if(!engine || !engine->IsA(actor,"Actor"))return -1;
    if(engine->IsA(actor,"DominionPlayerCharacter")){
        auto component=Mem::ReadPtr(actor+DomEngine::Offsets::DomPlayerChar_HealthComponent);
        if(!component || !Mem::Readable((void*)component,0x148) || Mem::ReadPtr(component+DomEngine::Offsets::UObject_Outer)!=actor || !engine->IsA(component,"HealthComponent"))return -1;
        float max=Mem::ReadFloat(component+DomEngine::Offsets::Health_MaxHealth,-1),hp=Mem::ReadFloat(component+DomEngine::Offsets::Health_AuthoritativeHealth,-1);
        return std::isfinite(max) && std::isfinite(hp) && max>0 && max<=1e7 && hp>=0 && hp<=max*1.25f?hp:-1;
    }
    uintptr_t found=0;float hp=-1;
    for(uintptr_t off=0x200;off<0x1800;off+=8) {
        uintptr_t component=Mem::ReadPtr(actor+off);
        if(!component || !Mem::Readable((void*)component,0x148) || Mem::ReadPtr(component+DomEngine::Offsets::UObject_Outer)!=actor || !engine->IsA(component,"HealthComponent"))continue;
        float max=Mem::ReadFloat(component+DomEngine::Offsets::Health_MaxHealth,-1);
        float value=Mem::ReadFloat(component+DomEngine::Offsets::Health_AuthoritativeHealth,-1);
        if(!std::isfinite(max) || !std::isfinite(value) || max<=0 || max>1e7 || value<0 || value>max*1.25f)continue;
        if(found && found!=component)return -1;
        found=component;hp=value;
    }
    return hp;
}
std::string Cause(const Raw& record,const std::map<uintptr_t,const DomEngine::PlayerInfo*>& pawns){
    auto* engine=DomEngine::g_Engine;if(!engine || record.ambiguous)return {};
    std::string cause=record.explicitCause;
    if(cause.empty() && record.type && record.typeIndex>=0 && Mem::ReadI32(record.type+DomEngine::Offsets::UObject_Index,-1)==record.typeIndex)cause=EnvironmentalCause(engine->GetObjectName(record.type));
    if(cause.empty() && record.causer && record.causerIndex>=0 && Mem::ReadI32(record.causer+DomEngine::Offsets::UObject_Index,-1)==record.causerIndex){
        cause=EnvironmentalCause(engine->GetObjectName(record.causer));
        if(cause.empty())cause=EnvironmentalCause(engine->GetObjectClassName(record.causer));
    }
    uintptr_t attackerPawn=record.instigatorIndex>=0 && Mem::ReadI32(record.instigator+DomEngine::Offsets::UObject_Index,-1)==record.instigatorIndex?record.instigator:0;
    if(engine->IsA(attackerPawn,"Controller")){
        auto pawn=Mem::ReadPtr(attackerPawn+DomEngine::Offsets::Controller_Pawn);
        attackerPawn=pawn && Mem::ReadPtr(pawn+DomEngine::Offsets::Pawn_Controller)==attackerPawn?pawn:0;
    }
    if(cause.empty() && attackerPawn && attackerPawn!=record.victim){
        auto known=pawns.find(attackerPawn);
        if(known!=pawns.end())cause=known->second->characterName;
        else if(engine->IsA(attackerPawn,"DominionAICharacter"))cause=CreatureName(engine->GetObjectClassName(attackerPawn));
    }
    return cause;
}
}
bool Discover(uintptr_t function,Parameters& out) {
    const std::map<std::string,int> anchor={{"DamagedActor",0},{"BaseDamage",8},{"EventInstigator",16},{"DamageCauser",24},{"DamageTypeClass",32},{"ReturnValue",40}};
    Walk winner;int matches=0;
    for(uintptr_t name=0x08;name<=0x30;name+=8)for(uintptr_t next=0x08;next<=0x38;next+=8)for(uintptr_t offset=0x30;offset<=0x70;offset+=4) {
        Parameters candidate;Walk test{name,next,offset};
        if(!WalkFields(function,test,candidate) || candidate.fields!=anchor || !Validate(candidate))continue;
        winner=test;out=candidate;++matches;
    }
    if(matches!=1)return false;
    walk=winner;layoutReady=true;return true;
}
void Poll() {
    if(stopped || !GameThread::FrameLayoutReady() || !DomEngine::g_Engine)return;
    auto* engine=DomEngine::g_Engine;
    if(!layoutReady) {
        auto fn=engine->FindFunction("GameplayStatics","ApplyDamage");
        Parameters params;if(!fn || !Discover(fn,params))return;
    }
    const char* names[]={"ApplyDamage","ApplyPointDamage"};
    Thunk replacements[]={&Hook0,&Hook1};
    for(size_t i=0;i<hooks.size();++i) {
        auto& h=hooks[i];if(h.active)continue;
        auto fn=engine->FindFunction("GameplayStatics",names[i]);Parameters params;
        if(!fn || !WalkFields(fn,walk,params) || !Validate(params) || !(Mem::ReadI32(fn+0xB0)&0x400))continue;
        auto original=Mem::ReadPtr(fn+0xD8);if(!original || !Mem::InImage((void*)original))continue;
        h.function=fn;h.params=std::move(params);h.original=(Thunk)original;h.active=true;
        __atomic_store_n((uintptr_t*)(fn+0xD8),(uintptr_t)replacements[i],__ATOMIC_RELEASE);++armed;
    }
    if(!typedLayout.ready){
        typedLayout.name=walk.name;typedLayout.next=walk.next;typedLayout.offset=walk.offset;
        auto anchor=engine->FindFunction("GameplayStatics","ApplyDamage");
        if(!anchor || !DamageFields::Learn(anchor,typedLayout))return;
        typedReady=true;
    }
    auto arm=[&](Hook& hook,uintptr_t fn,const std::map<std::string,DamageFields::Field>& fields,int size,Thunk replacement){
        if(hook.active)return;
        auto original=Mem::ReadPtr(fn+0xD8);if(!original || !Mem::InImage((void*)original))return;
        // Shared thunks are never patched as a player-cause witness.
        for(const auto& generic:hooks)if(generic.original && original==(uintptr_t)generic.original)return;
        if(predictHook.active && original==(uintptr_t)predictHook.original)return;
        for(const auto& health:healthHooks)if(health.active && original==(uintptr_t)health.original)return;
        hook.function=fn;hook.params.size=size;hook.params.fields.clear();
        for(const auto& entry:fields)hook.params.fields[entry.first]=entry.second.offset;
        hook.original=(Thunk)original;hook.active=true;
        __atomic_store_n((uintptr_t*)(fn+0xD8),(uintptr_t)replacement,__ATOMIC_RELEASE);
    };
    if(!predictHook.active){
        auto fn=engine->FindFunction("DamageComponent","BP_PredictPointDamage");std::map<std::string,DamageFields::Field> fields;
        Json info={{"functionFound",fn!=0},{"fields",Json::array()},{"state","function not found"}};
        if(fn){
            auto shape=(uint32_t)Mem::ReadI32(fn+0xB4);
            info["parameterBytes"]=shape>>16;info["parameterCount"]=shape&0xff;info["flags"]=(uint32_t)Mem::ReadI32(fn+0xB0);
            for(auto address:DamageFields::Fields(fn,typedLayout))
                info["fields"].push_back({{"name",DamageFields::Name(address,typedLayout)},{"kind",DamageFields::Kind(address,typedLayout)},
                    {"offset",Mem::ReadI32(address+typedLayout.offset,-1)},{"size",Mem::ReadI32(address+typedLayout.size)},
                    {"arrayDim",Mem::ReadI32(address+typedLayout.size-4)}});
            bool layout=DamageFields::Read(fn,typedLayout,736,22,fields);
            bool types=layout && DamageFields::Is(fields,"Instigator","ObjectProperty",8) && DamageFields::Is(fields,"Source","ObjectProperty",8) && DamageFields::Is(fields,"ReturnValue","FloatProperty",4);
            info["layoutAccepted"]=layout;info["requiredTypesAccepted"]=types;
            if(types)arm(predictHook,fn,fields,736,&PredictThunk);
            info["state"]=predictHook.active?"armed":!layout?"parameter layout rejected":!types?"required field type rejected":"native thunk rejected (missing, outside executable image, or shared)";
        }
        std::lock_guard<std::mutex> lock(statusMutex);predictionDiagnostic=std::move(info);
    }
    const char* healthNames[]={"SetHealth","DecreaseHealth"};Thunk healthReplacements[]={&Health0,&Health1};
    for(size_t i=0;i<healthHooks.size();++i){
        if(healthHooks[i].active)continue;
        auto fn=engine->FindFunction("HealthComponent",healthNames[i]);std::map<std::string,DamageFields::Field> fields;
        const char* amount=i==0?"Value":"Amount";
        if(fn && DamageFields::Read(fn,typedLayout,24,2,fields) && DamageFields::Is(fields,amount,"FloatProperty",4) && DamageFields::Is(fields,"Context","StrProperty",16))arm(healthHooks[i],fn,fields,24,healthReplacements[i]);
    }
}
std::vector<Hit> Drain(const std::vector<DomEngine::PlayerInfo>& players,double now) {
    std::deque<Raw> raw;
    {std::lock_guard<std::mutex> lock(queueMutex);raw.swap(queue);}
    now=std::max(now,Now());
    std::vector<Hit> result;auto* engine=DomEngine::g_Engine;if(!engine)return result;
    std::map<uintptr_t,const DomEngine::PlayerInfo*> controllers,pawns;
    for(const auto& p:players){if(p.controllerPtr)controllers[p.controllerPtr]=&p;if(p.pawnPtr)pawns[p.pawnPtr]=&p;}
    // Preserve last observed damage per victim; a later unbound hit must not
    // accidentally leave an earlier player's claim as the winning record.
    for(const auto& record:raw)if(record.predicted){if(predictions.size()<512)predictions.push_back(record);else ++dropped;}
    std::map<uintptr_t,Raw> latest;
    auto remember=[&](const Raw& record){
        auto previous=latest.find(record.victim);
        if(previous==latest.end() || (record.at>=previous->second.at && !(record.at-previous->second.at<=0.5 && !previous->second.explicitCause.empty() && record.explicitCause.empty())))latest[record.victim]=record;
    };
    auto accept=[&](const Raw& record){
        auto player=pawns.find(record.victim);
        if(record.before>0 && record.after==0 && player!=pawns.end() && player->second->hasHealth && !player->second->isDead){
            if(predictions.size()<512)predictions.push_back(record);else ++dropped;
        }else remember(record);
    };
    for(auto record:raw){
        if(record.predicted)continue;
        accept(record);
    }
    std::map<uintptr_t,std::set<std::string>> predictedCauses;
    for(const auto& prediction:predictions){
        auto player=pawns.find(prediction.victim);
        if(prediction.predicted && prediction.before>0 && prediction.damage+0.01f>=prediction.before && now>=prediction.at && now-prediction.at<=2.5 && player!=pawns.end() && player->second->isDead)predictedCauses[prediction.victim].insert(Cause(prediction,pawns));
    }
    for(auto it=predictions.begin();it!=predictions.end();){
        auto player=pawns.find(it->victim);
        if(now<it->at || now-it->at>2.5 || player==pawns.end() || Mem::ReadI32(it->victim+DomEngine::Offsets::UObject_Index,-1)!=it->index){++predictExpired;it=predictions.erase(it);continue;}
        if(!it->predicted && it->after==0){
            if(!player->second->hasHealth || !player->second->isDead){++it;continue;}
            remember(*it);it=predictions.erase(it);continue;
        }
        if(!player->second->hasHealth || player->second->health>=it->before-0.01f){++it;continue;}
        auto authoritative=NpcHealth(it->victim);
        if(authoritative<0 || authoritative>=it->before-0.01f || (player->second->isDead && authoritative>0)){++it;continue;}
        auto landed=*it;landed.after=authoritative;
        auto possibilities=predictedCauses.find(landed.victim);
        if(possibilities!=predictedCauses.end() && (possibilities->second.size()!=1 || possibilities->second.count("")))landed.ambiguous=true;
        if(landed.damage+0.01f<landed.before)landed.before=-1; // nonlethal prediction cannot settle a later death
        remember(landed);it=predictions.erase(it);
    }
    for(const auto& entry:latest) {
        const auto& record=entry.second;
        if(now-record.at>5 || now<record.at || Mem::ReadI32(record.victim+DomEngine::Offsets::UObject_Index,-1)!=record.index)continue;
        Hit hit;hit.observed=record.at;hit.victimId=std::to_string(record.victim)+":"+std::to_string(record.index);
        auto attacker=controllers.find(record.instigator);if(attacker==controllers.end()) {
            auto pawn=pawns.find(record.instigator);if(pawn!=pawns.end())hit.attackerGuid=pawn->second->characterGuid;
        } else hit.attackerGuid=attacker->second->characterGuid;
        hit.weapon=engine->GetObjectClassName(record.causer);hit.cause=hit.weapon;
        auto victim=pawns.find(record.victim);
        if(victim!=pawns.end()){
            hit.victimGuid=victim->second->characterGuid;
            hit.fatalConfirmed=!record.predicted && record.before>0 && record.after==0 && victim->second->hasHealth && victim->second->isDead;
            if(hit.fatalConfirmed){
                std::string cause=Cause(record,pawns);
                if(!cause.empty()){
                    hit.cause=cause;hit.causeSettled=true;
                    ++settledFatal;
                    hit.causeEvidence=!record.explicitCause.empty()?"explicit health context and confirmed fatal health change":record.predicted?"typed damage prediction, lethal amount and observed death":"observed fatal health change with identified damage source";
                }
                if(!hit.causeSettled)++unresolvedFatal;
            }
        }
        else {
            float health=NpcHealth(record.victim);
            if(health<0){++healthMissing;continue;}
            hit.npcDeath=health==0;hit.victimName=engine->GetObjectClassName(record.victim);
            if(hit.npcDeath && hit.attackerGuid.empty())++unresolved;
        }
        result.push_back(std::move(hit));
    }
    return result;
}
DamageFields::Layout ReflectionLayout(){return typedLayout;}
Json Status(){std::lock_guard<std::mutex> lock(statusMutex);return {{"predictionDiagnostic",predictionDiagnostic},{"armedFunctions",armed.load()},{"layoutValidated",layoutReady.load()},{"captured",captured.load()},{"dropped",dropped.load()},
    {"unattributed",unresolved.load()},{"missingHealthEvidence",healthMissing.load()},{"typedFieldLayoutValidated",typedReady.load()},{"playerDamagePredictionHook",predictHook.active.load()},{"predictionCalls",predictCalls.load()},{"healthContextCalls",healthCalls.load()},{"predictionCaptures",predictCaptured.load()},{"expiredPredictions",predictExpired.load()},{"healthContextHooks",(healthHooks[0].active?1:0)+(healthHooks[1].active?1:0)},{"healthContextCaptures",healthCaptured.load()},{"settledFatalEvidence",settledFatal.load()},{"unresolvedFatalEvidence",unresolvedFatal.load()},
    {"coverage","Generic damage plus signature/type-validated player prediction and health-context witnesses; unobserved causes stay unresolved"}};}
void Stop(){stopped=true;}
}
