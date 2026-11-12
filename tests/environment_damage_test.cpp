#include "thc/environment_damage.h"
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
std::vector<std::pair<uintptr_t,size_t>> regions;std::vector<std::string> names={"None"};
void Register(void* p,size_t n){regions.push_back({(uintptr_t)p,n});}
uint32_t Name(const std::string& s){for(size_t i=0;i<names.size();++i)if(names[i]==s)return i;names.push_back(s);return names.size()-1;}
alignas(uintptr_t) unsigned char root[0x70]{},wrapper[0x70]{},element[0x70]{},enumeration[0xC0]{},props[6][0xC0]{},kinds[6][0x30]{},tagProps[4][0xC0]{},tagContainer[0x70]{},tagType[0x70]{},tagValues[2][8]{},enumValues[3][16]{},data[96]{},values[2][32]{};
uintptr_t Kind(const char* text){for(auto& k:kinds)if(*(uint32_t*)k==Name(text))return (uintptr_t)k;for(auto& k:kinds)if(!*(uint32_t*)k){*(uint32_t*)k=Name(text);return (uintptr_t)k;}assert(false);return 0;}
void Field(unsigned char* p,const char* name,const char* kind,int offset,int size,unsigned char* next=nullptr){*(uint32_t*)(p+0x28)=Name(name);*(uintptr_t*)(p+8)=Kind(kind);*(int*)(p+0x4C)=offset;*(int*)(p+0x3C)=size;*(int*)(p+0x38)=1;*(uintptr_t*)(p+0x20)=(uintptr_t)next;}
void Array(int num=1,int max=1,void* pointer=values){FUEArray a{pointer,num,max};std::memcpy(data+40,&a,sizeof(a));}
}
namespace Mem {bool Readable(const void* p,size_t n){auto a=(uintptr_t)p;for(auto r:regions)if(a>=r.first && a-r.first<=r.second && n<=r.second-(a-r.first))return true;return false;}}
namespace DomEngine {
Engine* g_Engine=nullptr;
std::string Engine::NameToString(uint32_t id,uint32_t)const{return id<names.size()?names[id]:"";}
bool Engine::IsA(uintptr_t object,const char* kind)const{return ((object==(uintptr_t)root || object==(uintptr_t)wrapper || object==(uintptr_t)element || object==(uintptr_t)tagContainer || object==(uintptr_t)tagType) && std::string(kind)=="ScriptStruct") || (object==(uintptr_t)enumeration && std::string(kind)=="Enum");}
std::string Engine::GetObjectName(uintptr_t object)const{return object==(uintptr_t)root?"DominionDamageEvent":object==(uintptr_t)wrapper?"TypeDamageArray":object==(uintptr_t)element?"TypeDamage":object==(uintptr_t)enumeration?"EStatusEffectType":object==(uintptr_t)tagContainer?"GameplayTagContainer":object==(uintptr_t)tagType?"GameplayTag":"";}
}
int main(){
 for(auto r:{std::pair<void*,size_t>{root,sizeof(root)},{wrapper,sizeof(wrapper)},{element,sizeof(element)},{enumeration,sizeof(enumeration)},{props,sizeof(props)},{kinds,sizeof(kinds)},{enumValues,sizeof(enumValues)},{data,sizeof(data)},{values,sizeof(values)},{tagProps,sizeof(tagProps)},{tagContainer,sizeof(tagContainer)},{tagType,sizeof(tagType)},{tagValues,sizeof(tagValues)}})Register(r.first,r.second);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;
 Thc::DamageFields::Layout reflection{0x28,0x20,0x4C,0x3C,8,0,true};Thc::ReflectedDamage::EventLayout event;event.scriptStruct=(uintptr_t)root;event.bytes=sizeof(data);
 Field(props[0],"TypeDamagesArray","StructProperty",32,24);*(uintptr_t*)(props[0]+0x78)=(uintptr_t)wrapper;*(uintptr_t*)(root+0x50)=(uintptr_t)props[0];
 Field(props[1],"TypeDamages","ArrayProperty",8,16);*(uintptr_t*)(props[1]+0x78)=(uintptr_t)props[2];*(uintptr_t*)(wrapper+0x50)=(uintptr_t)props[1];
 Field(props[2],"TypeDamages","StructProperty",0,32);*(uintptr_t*)(props[2]+0x78)=(uintptr_t)element;
 Field(props[3],"StatusEffect","EnumProperty",0,1,props[4]);*(uintptr_t*)(props[3]+0x80)=(uintptr_t)enumeration;Field(props[4],"Amount","FloatProperty",4,4);*(uintptr_t*)(element+0x50)=(uintptr_t)props[3];
 const char* enums[]={"EStatusEffectType::None","EStatusEffectType::Burning","EStatusEffectType::Poison"};int ids[]={0,42,7};
 for(int i=0;i<3;++i){*(uint32_t*)enumValues[i]=Name(enums[i]);*(int64_t*)(enumValues[i]+8)=ids[i];}
 FUEArray enumNames{enumValues,3,3};std::memcpy(enumeration+0x40,&enumNames,sizeof(enumNames));
 Array();values[0][0]=42;*(float*)(values[0]+4)=5;std::string reason;
 auto read=[&]{return Thc::EnvironmentDamage::Read((uintptr_t)data,event,reflection,reason);};
 assert(read()=="Fire");values[0][0]=7;assert(read()=="Poison");values[0][0]=99;assert(read().empty());values[0][0]=0;assert(read().empty());
 values[0][0]=42;*(float*)(values[0]+4)=0;assert(read().empty());*(float*)(values[0]+4)=5;
 Array(2,2);values[1][0]=7;*(float*)(values[1]+4)=4;assert(read().empty());values[1][0]=42;assert(read()=="Fire");Array();
 *(float*)(values[0]+4)=-1;assert(read().empty());*(float*)(values[0]+4)=5;
 Array(65,65);assert(read().empty());Array(2,1);assert(read().empty());Array(1,1,(void*)3);assert(read().empty());Array();
 std::memcpy(enumeration+0x60,&enumNames,sizeof(enumNames));assert(read().empty());std::memset(enumeration+0x60,0,sizeof(enumNames));
 *(uintptr_t*)(props[3]+0x80)=0;assert(read().empty());*(uintptr_t*)(props[3]+0x80)=(uintptr_t)enumeration;
 *(int*)(props[4]+0x4C)=31;assert(read().empty());*(int*)(props[4]+0x4C)=4;
 // Direct environmental damage may use a damage tag instead of a status enum.
 Field(tagProps[0],"Types","StructProperty",8,16);*(uintptr_t*)(props[4]+0x20)=(uintptr_t)tagProps[0];*(uintptr_t*)(tagProps[0]+0x78)=(uintptr_t)tagContainer;
 Field(tagProps[1],"GameplayTags","ArrayProperty",0,16);*(uintptr_t*)(tagContainer+0x50)=(uintptr_t)tagProps[1];*(uintptr_t*)(tagProps[1]+0x78)=(uintptr_t)tagProps[2];
 Field(tagProps[2],"GameplayTags","StructProperty",0,8);*(uintptr_t*)(tagProps[2]+0x78)=(uintptr_t)tagType;
 Field(tagProps[3],"TagName","NameProperty",0,8);*(uintptr_t*)(tagType+0x50)=(uintptr_t)tagProps[3];
 FUEArray tags{tagValues,1,2};std::memcpy(values[0]+8,&tags,sizeof(tags));values[0][0]=0;
 *(uint32_t*)tagValues[0]=Name("Damage.Type.Fire");assert(read()=="Fire");
 *(uint32_t*)tagValues[0]=Name("Damage.Type.FireResistance");assert(read().empty());
 *(uint32_t*)tagValues[0]=Name("Damage.Type.Fire");*(uint32_t*)tagValues[1]=Name("Damage.Type.Poison");tags.Num=2;std::memcpy(values[0]+8,&tags,sizeof(tags));assert(read().empty());
 tags.Num=1;tags.Data=(void*)3;std::memcpy(values[0]+8,&tags,sizeof(tags));assert(read().empty());
 assert(Thc::EnvironmentDamage::Label("EStatusEffectType::FrostBite")=="Frost Bite");assert(Thc::EnvironmentDamage::Label("EStatusEffectType::MAX").empty());
 std::cout<<"PASS: reflected environmental damage, dynamically learned enum values, Burning/Poison labels, mixed/unknown/zero/malformed refusal and bounds checks\n";
}
