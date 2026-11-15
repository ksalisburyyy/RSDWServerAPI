// Exercise the production enumeration code over registered fake objects.
#include "engine/players.cpp"
#include "api/serialize.cpp"
#include <cassert>
#include <iostream>
#include <utility>
#include <limits>

APIConfig g_Config;
namespace {
alignas(uintptr_t) unsigned char world[0x4000]{}, state[0x4000]{},
    pawn[0x4000]{}, respawn[0x4000]{}, food[0x400]{}, water[0x400]{},
    displayNames[0x400]{}, nameEntry[0x400]{}, healthComponent[0x400]{},
    attributesComponent[0x400]{}, endurance[0x400]{}, duplicate[0x400]{},
    healthAttribute[0x100]{}, maxHealthAttribute[0x100]{};
uintptr_t attributes[]={(uintptr_t)healthAttribute,(uintptr_t)maxHealthAttribute};
float attributeValues[]={210,210};
std::string duplicateType="EnduranceComponent";
uintptr_t states[] = {(uintptr_t)state};
std::vector<std::pair<uintptr_t,size_t>> regions;
int objectReads=0;
uintptr_t currentWorld=(uintptr_t)world;
std::string guid(32,'A'), character="Original";
}
namespace Mem {
bool Readable(const void* p,size_t n){
    auto a=(uintptr_t)p;
    for(auto r:regions)if(a>=r.first && a-r.first<=r.second && n<=r.second-(a-r.first))return true;
    return false;
}
}
namespace DomEngine {
bool Engine::Initialize(){initialized=true;return true;}
uintptr_t Engine::GetGameState()const{return currentWorld;}
int32_t Engine::GetObjectCount()const{return 100000;}
uintptr_t Engine::GetObjectByIndex(int32_t i)const{
    ++objectReads;
    switch(i){
    case 123:return (uintptr_t)food;
    case 200:return (uintptr_t)healthComponent;
    case 300:return (uintptr_t)attributesComponent;
    case 400:return (uintptr_t)endurance;
    case 500:return (uintptr_t)duplicate;
    case 99999:return (uintptr_t)water;
    default:return 0;
    }
}
bool Engine::IsA(uintptr_t p,const char* name)const{
    std::string n=name;
    return (p==(uintptr_t)world && n=="DominionGameStateBase") ||
           (p==(uintptr_t)state && n=="DominionPlayerState") ||
           (p==(uintptr_t)food && n=="SustenanceComponent") ||
           (p==(uintptr_t)water && n=="HydrationComponent") ||
           (p==(uintptr_t)pawn && n=="DominionPlayerCharacter") ||
           (p==(uintptr_t)healthComponent && n=="PlayerHealthComponent") ||
           (p==(uintptr_t)attributesComponent && n=="DominionAttributesComponent") ||
           (p==(uintptr_t)endurance && n=="EnduranceComponent") ||
           (p==(uintptr_t)duplicate && n==duplicateType);
}
std::string Engine::ReadFString(uintptr_t p)const{return p==(uintptr_t)nameEntry+Offsets::CharDisplayName_CharacterName?character:p==(uintptr_t)state+Offsets::PlayerState_PlayerName?"Platform":"";}
std::string Engine::ReadGuid(uintptr_t)const{return guid;}
std::string Engine::ReadUniqueNetId(uintptr_t,std::string&)const{return "fixture-net-id";}
std::string Engine::GetObjectClassName(uintptr_t p)const{
 if(p==(uintptr_t)healthAttribute)return "HealthAttribute";
 if(p==(uintptr_t)maxHealthAttribute)return "MaxHealthAttribute";
 return "FixturePawn";
}
std::string Engine::GetObjectName(uintptr_t)const{return "FixtureObject";}
}
int main(){
    g_Config.enableLogging=false;
    for(auto r:{std::pair<void*,size_t>{world,sizeof(world)},{state,sizeof(state)},
        {pawn,sizeof(pawn)},{respawn,sizeof(respawn)},{food,sizeof(food)},
        {water,sizeof(water)},{states,sizeof(states)},{displayNames,sizeof(displayNames)},{nameEntry,sizeof(nameEntry)},
        {healthComponent,sizeof(healthComponent)},{attributesComponent,sizeof(attributesComponent)},
        {endurance,sizeof(endurance)},{duplicate,sizeof(duplicate)},
        {healthAttribute,sizeof(healthAttribute)},{maxHealthAttribute,sizeof(maxHealthAttribute)},
        {attributes,sizeof(attributes)},{attributeValues,sizeof(attributeValues)}})regions.push_back({(uintptr_t)r.first,r.second});
    using namespace DomEngine;
    FUEArray array{states,1,1};
    memcpy(world+Offsets::GameStateBase_PlayerArray,&array,sizeof(array));
    *(uintptr_t*)(world+Offsets::DomGameState_DisplayNameComponent)=(uintptr_t)displayNames;
    FUEArray names{nameEntry,1,1};
    memcpy(displayNames+Offsets::DisplayName_OriginalCharacterNames,&names,sizeof(names));
    *(uintptr_t*)(state+Offsets::PlayerState_PawnPrivate)=(uintptr_t)pawn;
    *(uintptr_t*)(food+Offsets::UObject_Outer)=(uintptr_t)pawn;
    *(uintptr_t*)(water+Offsets::UObject_Outer)=(uintptr_t)pawn;
    *(float*)(food+Offsets::SurvivalStat_Current)=35;
    *(float*)(water+Offsets::SurvivalStat_Current)=72;
    for(auto component:{healthComponent,attributesComponent,endurance})
        *(uintptr_t*)(component+Offsets::UObject_Outer)=(uintptr_t)pawn;
    healthComponent[Offsets::Health_bCanDie]=1;
    // Raw health and obsolete SDK pawn offsets intentionally remain zero.
    FUEArray attributeArray{attributes,2,2}, valueArray{attributeValues,2,2};
    memcpy(attributesComponent+Offsets::Attributes_SharedFloatAttributes,&attributeArray,sizeof(attributeArray));
    memcpy(attributesComponent+Offsets::Attributes_SharedAttributeValues,&valueArray,sizeof(valueArray));
    *(float*)(endurance+Offsets::Endurance_Current)=89.8f;
    *(float*)(endurance+Offsets::Endurance_Max)=100;
    *(float*)(endurance+Offsets::SurvivalStat_CurrentMax)=0; // Endurance decay buffer.
    Engine engine;
    assert(engine.GetPlayerIdentities().empty());
    engine.Initialize();
    auto full=engine.GetAllPlayers();
    assert(objectReads==100000 && full.size()==1);
    assert(full[0].hasSustenance && full[0].sustenance==35);
    assert(full[0].hasHydration && full[0].hydration==72);
    assert(full[0].hasHealth && full[0].health==210 && full[0].maxHealth==210 && full[0].canDie && !full[0].isDead);
    assert(full[0].hasRest && full[0].rest==89.8f && full[0].maxRest==100);
    auto json=Serialize::Player(full[0]);
    assert(json.find("\"health\":{\"current\":210.000,\"max\":210.000")!=std::string::npos);
    assert(json.find("\"rest\":{\"current\":89.800,\"max\":100.000}")!=std::string::npos);
    // Attribute health remains available when the optional health component is absent.
    *(uintptr_t*)(healthComponent+Offsets::UObject_Outer)=0;
    auto detail=engine.GetAllPlayers();assert(detail[0].hasHealth && !detail[0].canDie);
    *(uintptr_t*)(healthComponent+Offsets::UObject_Outer)=(uintptr_t)pawn;
    // A component belonging to someone else must never supply this player's values.
    *(uintptr_t*)(attributesComponent+Offsets::UObject_Outer)=(uintptr_t)respawn;
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth && !detail[0].isDead);
    *(uintptr_t*)(attributesComponent+Offsets::UObject_Outer)=(uintptr_t)pawn;
    *(uintptr_t*)(endurance+Offsets::UObject_Outer)=(uintptr_t)respawn;
    detail=engine.GetAllPlayers();assert(!detail[0].hasRest && detail[0].hasHydration);
    *(uintptr_t*)(endurance+Offsets::UObject_Outer)=(uintptr_t)pawn;
    // Duplicate matching components are ambiguous; do not choose one arbitrarily.
    *(uintptr_t*)(duplicate+Offsets::UObject_Outer)=(uintptr_t)pawn;
    detail=engine.GetAllPlayers();assert(!detail[0].hasRest && detail[0].hasHealth);
    duplicateType="DominionAttributesComponent";
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth && detail[0].hasRest);
    *(uintptr_t*)(duplicate+Offsets::UObject_Outer)=0;
    // Inconsistent/unreadable attribute arrays must fail closed rather than report a death.
    valueArray.Num=1;memcpy(attributesComponent+Offsets::Attributes_SharedAttributeValues,&valueArray,sizeof(valueArray));
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth && !detail[0].isDead);
    valueArray.Num=2;valueArray.Max=1;memcpy(attributesComponent+Offsets::Attributes_SharedAttributeValues,&valueArray,sizeof(valueArray));
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth);
    valueArray.Max=2;valueArray.Data=(void*)3;memcpy(attributesComponent+Offsets::Attributes_SharedAttributeValues,&valueArray,sizeof(valueArray));
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth);
    valueArray.Data=attributeValues;memcpy(attributesComponent+Offsets::Attributes_SharedAttributeValues,&valueArray,sizeof(valueArray));
    attributeValues[0]=std::numeric_limits<float>::quiet_NaN();
    detail=engine.GetAllPlayers();assert(!detail[0].hasHealth && !detail[0].isDead);
    attributeValues[0]=0;detail=engine.GetAllPlayers();assert(detail[0].hasHealth && detail[0].isDead);
    attributeValues[0]=210;
    for(float invalid:{-1.f,101.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        *(float*)(endurance+Offsets::Endurance_Current)=invalid;
        detail=engine.GetAllPlayers();assert(!detail[0].hasRest);
        assert(Serialize::Player(detail[0]).find("\"rest\":null")!=std::string::npos);
    }
    *(float*)(endurance+Offsets::Endurance_Current)=0;
    detail=engine.GetAllPlayers();assert(detail[0].hasRest && detail[0].rest==0);
    *(float*)(endurance+Offsets::Endurance_Max)=0;
    detail=engine.GetAllPlayers();assert(!detail[0].hasRest);
    *(float*)(endurance+Offsets::Endurance_Max)=100;
    *(float*)(endurance+Offsets::Endurance_Current)=89.8f;
    objectReads=0;
    auto ids=engine.GetPlayerIdentities();
    assert(ids.size()==1 && objectReads==0);
    assert(ids[0].characterGuid==full[0].characterGuid && ids[0].name==full[0].name);
    assert(ids[0].characterName==character);
    assert(ids[0].pawnPtr==full[0].pawnPtr && ids[0].playerStatePtr==full[0].playerStatePtr);
    assert(!ids[0].hasHydration && !ids[0].hasSustenance && !ids[0].hasHealth && !ids[0].hasRest && ids[0].skills.empty());
    // Bindings must reflect a rename, character switch, respawn and despawn immediately.
    character="Renamed";guid=std::string(32,'B');
    *(uintptr_t*)(state+Offsets::PlayerState_PawnPrivate)=(uintptr_t)respawn;
    ids=engine.GetPlayerIdentities();
    assert(ids[0].characterName==character && ids[0].characterGuid==guid && ids[0].pawnPtr==(uintptr_t)respawn && objectReads==0);
    *(uintptr_t*)(state+Offsets::PlayerState_PawnPrivate)=0;
    ids=engine.GetPlayerIdentities();assert(ids.size()==1 && !ids[0].spawned && !ids[0].pawnPtr);
    states[0]=3;assert(engine.GetPlayerIdentities().empty());states[0]=(uintptr_t)state;
    array.Num=0;memcpy(world+Offsets::GameStateBase_PlayerArray,&array,sizeof(array));
    assert(engine.GetPlayerIdentities().empty());
    array.Num=513;memcpy(world+Offsets::GameStateBase_PlayerArray,&array,sizeof(array));
    assert(engine.GetPlayerIdentities().empty());
    currentWorld=0;assert(engine.GetPlayerIdentities().empty() && objectReads==0);
    std::cout<<"PASS: fresh identities skip 100000 object reads; typed/owned HP 210 and rest 89.8/100, malformed/ambiguous data refusal, lifecycle safety preserved\n";
}
