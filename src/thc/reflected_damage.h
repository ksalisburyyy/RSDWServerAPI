#pragma once
#include "damage_fields.h"
#include <cmath>
#include <limits>

namespace Thc::ReflectedDamage {
struct Property { uintptr_t address=0; int offset=-1,size=0; std::string kind; };
struct EventLayout {
    uintptr_t scriptStruct=0;
    int bytes=0,amount=-1,instigator=-1,source=-1,id=-1;
    std::map<std::string,DamageFields::Field> fields;
};
struct Snapshot {
    float amount=0;
    uintptr_t instigator=0,source=0;
    int id=0,instigatorIndex=-1,sourceIndex=-1;
    bool unusualAmount=false,instigatorUnavailable=false,sourceUnavailable=false;
};
inline bool Fail(std::string& reason,const char* value){reason=value;return false;}

// Follow only already learned FField links and standard UStruct inheritance.
// Every node is guarded. Duplicate names, cycles, oversized chains, invalid
// dimensions and overlapping non-bitfield storage refuse the whole layout.
inline bool Collect(uintptr_t owner,const DamageFields::Layout& layout,int bound,
                    std::map<std::string,Property>& out,std::string& reason){
    out.clear();reason.clear();
    auto* engine=DomEngine::g_Engine;
    if(!engine || !layout.ready || bound<1 || bound>65536)return Fail(reason,"unavailable reflection layout or invalid container bound");
    if(layout.name>0x70 || layout.next>0x70 || layout.offset>0x74 || layout.size<4 || layout.size>0x74 || layout.kind>0x70 || layout.kindName>0x70)
        return Fail(reason,"reflection member offsets out of bounds");
    std::set<uintptr_t> owners,fields;
    std::vector<std::tuple<int,int,std::string>> spans;
    int depth=0;
    while(owner){
        if(++depth>32 || !owners.insert(owner).second || !Mem::Readable((void*)owner,0x58))return Fail(reason,"invalid or cyclic reflected inheritance");
        uintptr_t field=Mem::ReadPtr(owner+0x50);int ownFields=0;
        while(field){
            if(++ownFields>256 || fields.size()>=512 || !fields.insert(field).second || !Mem::Readable((void*)field,0x78))
                return Fail(reason,"invalid, cyclic or oversized property chain");
            Property property;property.address=field;
            auto name=DamageFields::Name(field,layout);property.kind=DamageFields::Kind(field,layout);
            property.offset=Mem::ReadI32(field+layout.offset,-1);
            int element=Mem::ReadI32(field+layout.size,-1),dim=Mem::ReadI32(field+layout.size-4,-1);
            if(name.empty() || name=="None" || name.size()>128 || property.kind.empty() || property.kind=="None" ||
               property.offset<0 || element<1 || element>bound || dim<1 || dim>256 || element>bound/dim)
                return Fail(reason,"invalid property name, type, offset, size or dimension");
            property.size=element*dim;
            if(property.offset>bound-property.size || !out.emplace(name,property).second)return Fail(reason,"property outside container or duplicate inherited name");
            spans.emplace_back(property.offset,property.offset+property.size,property.kind);
            field=Mem::ReadPtr(field+layout.next);
        }
        owner=Mem::ReadPtr(owner+DomEngine::Offsets::UStruct_SuperStruct);
    }
    std::sort(spans.begin(),spans.end());
    for(size_t i=1;i<spans.size();++i){
        const auto& a=spans[i-1];const auto& b=spans[i];
        if(std::get<0>(b)>=std::get<1>(a))continue;
        const bool packed=std::get<2>(a)=="BoolProperty" && std::get<2>(b)=="BoolProperty" &&
            std::get<0>(a)==std::get<0>(b) && std::get<1>(a)==std::get<1>(b) && std::get<1>(a)==std::get<0>(a)+1;
        if(!packed)return Fail(reason,"overlapping reflected property storage");
    }
    return true;
}
inline bool FindProperty(uintptr_t ownerClass,const DamageFields::Layout& layout,const std::string& name,int containerBytes,
                         Property& out,std::string& reason){
    out={};std::map<std::string,Property> properties;
    if(!Collect(ownerClass,layout,containerBytes,properties,reason))return false;
    auto it=properties.find(name);if(it==properties.end())return Fail(reason,"required reflected property missing");
    out=it->second;return true;
}
inline bool LearnStruct(const Property& property,const DamageFields::Layout& layout,const std::string& expectedStructName,
                        EventLayout& out,std::string& reason,bool requireID=true){
    out={};reason.clear();auto* engine=DomEngine::g_Engine;
    if(!engine || !layout.ready || property.kind!="StructProperty" || property.size<4 || property.size>4096 || !property.address)
        return Fail(reason,"damage event is not a bounded reflected struct");
    // FStructProperty's trailing UScriptStruct pointer is discovered by identity,
    // not by assuming the Windows ABI offset. More than one candidate refuses.
    uintptr_t found=0;int matches=0;
    for(uintptr_t offset=0x70;offset<=0xB0;offset+=8){
        uintptr_t candidate=0;
        if(!Mem::Read(property.address+offset,candidate) || !candidate || !Mem::Readable((void*)candidate,0x58))continue;
        if(engine->IsA(candidate,"ScriptStruct") && engine->GetObjectName(candidate)==expectedStructName){found=candidate;++matches;}
    }
    if(matches!=1)return Fail(reason,"damage-event script struct missing or ambiguous");
    std::map<std::string,Property> fields;
    if(!Collect(found,layout,property.size,fields,reason))return false;
    const std::tuple<const char*,const char*,int> required[]={
        {"Amount","FloatProperty",4},{"Instigator","ObjectProperty",8},{"Source","ObjectProperty",8},{"ID","IntProperty",4}};
    for(const auto& requirement:required){
        auto it=fields.find(std::get<0>(requirement));
        if(it==fields.end() && !requireID && std::string(std::get<0>(requirement))=="ID")continue;
        if(it==fields.end() || it->second.kind!=std::get<1>(requirement) || it->second.size!=std::get<2>(requirement) ||
           Mem::ReadI32(it->second.address+layout.size-4)!=1 || it->second.offset%std::get<2>(requirement))
            return Fail(reason,"damage-event required field type, size or alignment rejected");
    }
    out.scriptStruct=found;out.bytes=property.size;
    out.amount=fields.at("Amount").offset;out.instigator=fields.at("Instigator").offset;out.source=fields.at("Source").offset;out.id=fields.count("ID")?fields.at("ID").offset:-1;
    for(const auto& field:fields)out.fields[field.first]={field.second.offset,field.second.size,field.second.kind};
    return true;
}
inline bool Read(uintptr_t eventData,const EventLayout& layout,Snapshot& out,std::string& reason,bool deathTelemetry=false){
    out={};reason.clear();
    const std::pair<int,int> ranges[]={{layout.amount,4},{layout.instigator,8},{layout.source,8}};
    if(!layout.scriptStruct || layout.bytes<1 || layout.bytes>4096)return Fail(reason,"damage-event layout unavailable");
    for(const auto& range:ranges)if(range.first<0 || range.first>layout.bytes-range.second)return Fail(reason,"damage-event field outside validated layout");
    if(layout.id < -1 || (layout.id>=0 && layout.id>layout.bytes-4))return Fail(reason,"damage-event ID outside validated layout");
    if(!eventData || !Mem::Readable((void*)eventData,(size_t)layout.bytes))return Fail(reason,"damage-event storage unreadable");
    Snapshot value;int idAfter=0;
    if((layout.id>=0 && !Mem::Read(eventData+layout.id,value.id)) || !Mem::Read(eventData+layout.amount,value.amount) ||
       !Mem::Read(eventData+layout.instigator,value.instigator) || !Mem::Read(eventData+layout.source,value.source))
        return Fail(reason,"damage-event storage rejected");
    const bool normalAmount=std::isfinite(value.amount) && value.amount>0 && value.amount<=1000000;
    if(!normalAmount){
        if(!deathTelemetry)return Fail(reason,"damage-event amount rejected");
        // The validated death RPC is the death witness. Its numeric amount is
        // not used to infer damage, so sentinels/instant-kill values need not
        // satisfy ordinary hit limits. Canonicalize unused values for dedup.
        value.unusualAmount=true;value.amount=0;
    }
    auto index=[&](uintptr_t object,int& result){
        return !object || (Mem::Readable((void*)object,0x28) && Mem::Read(object+DomEngine::Offsets::UObject_Index,result) && result>=0);
    };
    if(!index(value.instigator,value.instigatorIndex)){
        if(!deathTelemetry)return Fail(reason,"damage-event instigator object unreadable");
        value.instigator=0;value.instigatorIndex=-1;value.instigatorUnavailable=true;
    }
    if(!index(value.source,value.sourceIndex)){
        if(!deathTelemetry)return Fail(reason,"damage-event source object unreadable");
        value.source=0;value.sourceIndex=-1;value.sourceUnavailable=true;
    }
    if(layout.id>=0 && (!Mem::Read(eventData+layout.id,idAfter) || idAfter!=value.id))return Fail(reason,"damage-event changed during read");
    // Pointer identity must still belong to the same object when the caller
    // later resolves it. The captured indexes are part of that contract.
    out=value;return true;
}
inline bool SameEvent(const Snapshot& a,const Snapshot& b){
    return a.id==b.id && a.amount==b.amount && a.instigator==b.instigator && a.source==b.source &&
        a.instigatorIndex==b.instigatorIndex && a.sourceIndex==b.sourceIndex;
}
}
