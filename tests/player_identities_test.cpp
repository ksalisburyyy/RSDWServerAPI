// Exercise the production enumeration code over registered fake objects.
#include "engine/players.cpp"
#include <cassert>
#include <iostream>
#include <utility>

APIConfig g_Config;
namespace {
alignas(uintptr_t) unsigned char world[0x4000]{}, state[0x4000]{},
    pawn[0x4000]{}, respawn[0x4000]{}, food[0x400]{}, water[0x400]{},
    displayNames[0x400]{}, nameEntry[0x400]{};
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
    return i==123?(uintptr_t)food:i==99999?(uintptr_t)water:0;
}
bool Engine::IsA(uintptr_t p,const char* name)const{
    std::string n=name;
    return (p==(uintptr_t)world && n=="DominionGameStateBase") ||
           (p==(uintptr_t)state && n=="DominionPlayerState") ||
           (p==(uintptr_t)food && n=="SustenanceComponent") ||
           (p==(uintptr_t)water && n=="HydrationComponent");
}
std::string Engine::ReadFString(uintptr_t p)const{return p==(uintptr_t)nameEntry+Offsets::CharDisplayName_CharacterName?character:p==(uintptr_t)state+Offsets::PlayerState_PlayerName?"Platform":"";}
std::string Engine::ReadGuid(uintptr_t)const{return guid;}
std::string Engine::ReadUniqueNetId(uintptr_t,std::string&)const{return "fixture-net-id";}
std::string Engine::GetObjectClassName(uintptr_t)const{return "FixturePawn";}
std::string Engine::GetObjectName(uintptr_t)const{return "FixtureObject";}
}
int main(){
    g_Config.enableLogging=false;
    for(auto r:{std::pair<void*,size_t>{world,sizeof(world)},{state,sizeof(state)},
        {pawn,sizeof(pawn)},{respawn,sizeof(respawn)},{food,sizeof(food)},
        {water,sizeof(water)},{states,sizeof(states)},{displayNames,sizeof(displayNames)},{nameEntry,sizeof(nameEntry)}})regions.push_back({(uintptr_t)r.first,r.second});
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
    Engine engine;
    assert(engine.GetPlayerIdentities().empty());
    engine.Initialize();
    auto full=engine.GetAllPlayers();
    assert(objectReads==100000 && full.size()==1);
    assert(full[0].hasSustenance && full[0].sustenance==35);
    assert(full[0].hasHydration && full[0].hydration==72);
    objectReads=0;
    auto ids=engine.GetPlayerIdentities();
    assert(ids.size()==1 && objectReads==0);
    assert(ids[0].characterGuid==full[0].characterGuid && ids[0].name==full[0].name);
    assert(ids[0].characterName==character);
    assert(ids[0].pawnPtr==full[0].pawnPtr && ids[0].playerStatePtr==full[0].playerStatePtr);
    assert(!ids[0].hasHydration && !ids[0].hasSustenance && !ids[0].hasHealth && ids[0].skills.empty());
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
    std::cout<<"PASS: fresh identities skip 100000 object reads; detailed survival data and lifecycle safety preserved\n";
}
