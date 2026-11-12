#pragma once
#include "../engine/dom_engine.h"
#include "../utils/memory.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <tuple>
namespace Thc::DamageFields {
struct Layout {uintptr_t name=0,next=0,offset=0,size=0,kind=0,kindName=0;bool ready=false;};
struct Field {int offset=-1,size=0;std::string kind;};
inline std::vector<uintptr_t> Fields(uintptr_t fn,const Layout& layout) {
    std::vector<uintptr_t> out;std::set<uintptr_t> seen;auto node=Mem::ReadPtr(fn+0x50);
    while(node && out.size()<64) {
        if(!seen.insert(node).second || !Mem::Readable((void*)node,0x78))return {};
        out.push_back(node);node=Mem::ReadPtr(node+layout.next);
    }
    if(node)return {};
    return out;
}
inline std::string Name(uintptr_t field,const Layout& layout) {
    auto* engine=DomEngine::g_Engine;if(!engine)return {};
    return engine->NameToString((uint32_t)Mem::ReadI32(field+layout.name),(uint32_t)Mem::ReadI32(field+layout.name+4));
}
inline std::string Kind(uintptr_t field,const Layout& layout) {
    auto* engine=DomEngine::g_Engine;auto type=Mem::ReadPtr(field+layout.kind);
    if(!engine || !type || !Mem::Readable((void*)(type+layout.kindName),8))return {};
    return engine->NameToString((uint32_t)Mem::ReadI32(type+layout.kindName),(uint32_t)Mem::ReadI32(type+layout.kindName+4));
}
inline bool Learn(uintptr_t anchor,Layout& layout) {
    const std::map<std::string,std::pair<int,std::string>> known={
        {"DamagedActor",{8,"ObjectProperty"}},{"BaseDamage",{4,"FloatProperty"}},
        {"EventInstigator",{8,"ObjectProperty"}},{"DamageCauser",{8,"ObjectProperty"}},
        {"DamageTypeClass",{8,"ClassProperty"}},{"ReturnValue",{4,"FloatProperty"}}};
    auto fields=Fields(anchor,layout);if(fields.size()!=known.size())return false;
    int sizeMatches=0;uintptr_t sizeOffset=0;
    for(uintptr_t size=0x2C;size<=0x68;size+=4) {
        bool match=true;
        for(auto field:fields) {
            auto it=known.find(Name(field,layout));
            if(it==known.end() || Mem::ReadI32(field+size)!=it->second.first || Mem::ReadI32(field+size-4)!=1){match=false;break;}
        }
        if(match){++sizeMatches;sizeOffset=size;}
    }
    if(sizeMatches!=1)return false;
    int kindMatches=0;Layout winner=layout;
    for(uintptr_t kind=8;kind<=0x28;kind+=8)for(uintptr_t name=0;name<=0x20;name+=4) {
        Layout candidate=layout;candidate.kind=kind;candidate.kindName=name;bool match=true;
        for(auto field:fields) {
            auto it=known.find(Name(field,layout));
            if(it==known.end() || Kind(field,candidate)!=it->second.second){match=false;break;}
        }
        if(match){winner=candidate;++kindMatches;}
    }
    if(kindMatches!=1)return false;
    winner.size=sizeOffset;winner.ready=true;layout=winner;return true;
}
inline bool Read(uintptr_t fn,const Layout& layout,int bytes,int count,std::map<std::string,Field>& out) {
    out.clear();if(!layout.ready)return false;
    auto shape=(uint32_t)Mem::ReadI32(fn+0xB4);
    if((int)(shape>>16)!=bytes || (int)(shape&0xff)!=count || (!(Mem::ReadI32(fn+0xB0)&0x400) || (Mem::ReadI32(fn+0xB0)&0x40)))return false;
    auto fields=Fields(fn,layout);if(fields.size()!=(size_t)count)return false;
    std::vector<std::tuple<int,int,std::string>> spans;
    for(auto address:fields) {
        auto name=Name(address,layout);Field field;
        field.offset=Mem::ReadI32(address+layout.offset,-1);field.size=Mem::ReadI32(address+layout.size);field.kind=Kind(address,layout);
        if(name.empty() || name=="None" || field.offset<0 || field.size<1 || field.size>bytes || field.offset>bytes-field.size || Mem::ReadI32(address+layout.size-4)!=1 || !out.emplace(name,field).second)return false;
        spans.push_back({field.offset,field.offset+field.size,field.kind});
    }
    std::sort(spans.begin(),spans.end());for(size_t i=1;i<spans.size();++i){
        if(std::get<0>(spans[i])<std::get<1>(spans[i-1])){
            bool packedBool=std::get<2>(spans[i])=="BoolProperty" && std::get<2>(spans[i-1])=="BoolProperty" && std::get<0>(spans[i])==std::get<0>(spans[i-1]) && std::get<1>(spans[i])==std::get<0>(spans[i])+1 && std::get<1>(spans[i-1])==std::get<1>(spans[i]);
            if(!packedBool)return false;
        }
    }
    return true;
}
inline bool Is(const std::map<std::string,Field>& fields,const std::string& name,const char* kind,int size) {
    auto it=fields.find(name);return it!=fields.end() && it->second.kind==kind && it->second.size==size && (size!=8 || it->second.offset%8==0);
}
}
