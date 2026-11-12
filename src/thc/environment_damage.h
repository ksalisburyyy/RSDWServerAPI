#pragma once
#include "reflected_damage.h"
#include <set>
#include <cctype>
namespace Thc::EnvironmentDamage {
inline std::string Label(std::string name){
 auto split=name.rfind("::");if(split!=std::string::npos)name.erase(0,split+2);
 static const std::map<std::string,std::string> labels={{"Burning","Fire"},{"Poison","Poison"},{"Bleeding","Bleeding"},{"Shock","Shock"},{"Starvation","Starvation"},{"Dehydration","Dehydration"},{"Toxicity","Toxicity"}};
 auto it=labels.find(name);if(it!=labels.end())return it->second;
 if(name.empty() || name=="None" || name=="Unknown" || name=="Invalid" || name.find("MAX")!=std::string::npos)return {};
 std::string readable;for(size_t i=0;i<name.size();++i){unsigned char c=name[i];
  if(c=='_'){if(!readable.empty() && readable.back()!=' ')readable+=' ';continue;}
  if(i && std::isupper(c) && std::islower((unsigned char)name[i-1]))readable+=' ';
  readable+=(char)c;
 }
 return readable;
}
inline uintptr_t Struct(const ReflectedDamage::Property& property,const DamageFields::Layout& layout,const std::string& name){
 auto* e=DomEngine::g_Engine;if(!e || property.kind!="StructProperty" || property.size<1 || property.size>4096)return 0;
 uintptr_t found=0;int matches=0;
 for(uintptr_t offset=0x70;offset<=0xB0;offset+=8){
  auto candidate=Mem::ReadPtr(property.address+offset);
  if(candidate && Mem::Readable((void*)candidate,0x58) && e->IsA(candidate,"ScriptStruct") && e->GetObjectName(candidate)==name){found=candidate;++matches;}
 }
 return matches==1?found:0;
}
inline bool Property(uintptr_t owner,const DamageFields::Layout& layout,int bytes,const char* name,const char* kind,int size,ReflectedDamage::Property& out){
 std::string reason;if(!ReflectedDamage::FindProperty(owner,layout,name,bytes,out,reason))return false;
 return out.kind==kind && (size<0 || out.size==size) && Mem::ReadI32(out.address+layout.size-4)==1;
}
inline uintptr_t Enum(const ReflectedDamage::Property& property){
 auto* e=DomEngine::g_Engine;if(!e || property.size!=1 || (property.kind!="EnumProperty" && property.kind!="ByteProperty"))return 0;
 uintptr_t found=0;int matches=0;
 for(uintptr_t offset=0x70;offset<=0xB0;offset+=8){auto candidate=Mem::ReadPtr(property.address+offset);
  if(candidate && Mem::Readable((void*)candidate,0x28) && e->IsA(candidate,"Enum") && e->GetObjectName(candidate)=="EStatusEffectType"){found=candidate;++matches;}
 }
 return matches==1?found:0;
}
// UEnum's names array is located by a unique, fully validated name/value table.
// Values are learned from the enum, never hard-coded from an old SDK dump.
inline bool EnumNames(uintptr_t enumeration,std::map<uint8_t,std::string>& out){
 auto* e=DomEngine::g_Engine;if(!e || !enumeration)return false;int matches=0;
 for(uintptr_t offset=0x28;offset<=0xA0;offset+=8){
  FUEArray array{};if(!Mem::Read(enumeration+offset,array) || array.Num<2 || array.Num>64 || array.Max<array.Num || array.Max>128 || !array.Data || !Mem::Readable(array.Data,(size_t)array.Num*16))continue;
  std::map<uint8_t,std::string> candidate;std::set<std::string> names;bool valid=true;
  for(int i=0;i<array.Num;++i){auto at=(uintptr_t)array.Data+(size_t)i*16;uint32_t id=0,number=0;int64_t value=0;
   if(!Mem::Read(at,id) || !Mem::Read(at+4,number) || !Mem::Read(at+8,value) || value<0 || value>255){valid=false;break;}
   auto name=e->NameToString(id,number);const std::string prefix="EStatusEffectType::";
   if(name.rfind(prefix,0)!=0){valid=false;break;}name.erase(0,prefix.size());
   if(name.empty() || name.size()>64 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos || !names.insert(name).second || !candidate.emplace((uint8_t)value,name).second){valid=false;break;}
  }
  if(valid && names.count("None") && names.count("Burning") && names.count("Poison")){out=std::move(candidate);++matches;}
 }
 return matches==1;
}
inline std::string TagCause(uintptr_t data,uintptr_t type,int bytes,const DamageFields::Layout& layout){
 ReflectedDamage::Property container,tags;
 if(!Property(type,layout,bytes,"Types","StructProperty",-1,container))return {};
 auto containerType=Struct(container,layout,"GameplayTagContainer");
 if(!containerType || !Property(containerType,layout,container.size,"GameplayTags","ArrayProperty",16,tags))return {};
 uintptr_t tagType=0;ReflectedDamage::Property inner;int matches=0;
 for(uintptr_t offset=0x70;offset<=0xB0;offset+=8){auto at=Mem::ReadPtr(tags.address+offset);if(!at || !Mem::Readable((void*)at,0x78))continue;
  ReflectedDamage::Property p{at,Mem::ReadI32(at+layout.offset,-1),Mem::ReadI32(at+layout.size,-1),DamageFields::Kind(at,layout)};
  auto candidate=Struct(p,layout,"GameplayTag");if(candidate && p.offset==0 && Mem::ReadI32(at+layout.size-4)==1){inner=p;tagType=candidate;++matches;}
 }
 ReflectedDamage::Property name;
 if(matches!=1 || !Property(tagType,layout,inner.size,"TagName","NameProperty",8,name))return {};
 FUEArray array{};
 if(!Mem::Read(data+container.offset+tags.offset,array) || array.Num<1 || array.Num>64 || array.Max<array.Num || array.Max>256 || !array.Data || !Mem::Readable(array.Data,(size_t)array.Num*inner.size))return {};
 const std::map<std::string,std::string> known={{"Burning","Fire"},{"Fire","Fire"},{"Heat","Heat"},{"Poison","Poison"},{"Bleeding","Bleeding"},{"Bleed","Bleeding"},{"Shock","Shock"},{"Cold","Cold"},{"Freezing","Freezing"},{"Starvation","Starvation"},{"Dehydration","Dehydration"},{"Toxicity","Toxicity"},{"Drowning","Drowning"},{"Fall","Fall damage"},{"FallDamage","Fall damage"},{"OutOfBounds","Out of bounds"}};
 std::set<std::string> causes;auto* e=DomEngine::g_Engine;
 for(int i=0;i<array.Num;++i){auto at=(uintptr_t)array.Data+(size_t)i*inner.size+name.offset;uint32_t id=0,number=0;
  if(!Mem::Read(at,id) || !Mem::Read(at+4,number))return {};
  auto tag=e->NameToString(id,number);auto dot=tag.rfind('.');if(dot!=std::string::npos)tag.erase(0,dot+1);
  auto it=known.find(tag);if(it!=known.end())causes.insert(it->second);
 }
 return causes.size()==1?*causes.begin():std::string{};
}
inline std::string Read(uintptr_t eventData,const ReflectedDamage::EventLayout& event,const DamageFields::Layout& layout,std::string& reason){
 auto* e=DomEngine::g_Engine;if(!e || !eventData || !layout.ready){reason="status reflection unavailable";return {};}
 ReflectedDamage::Property wrapper;
 if(!Property(event.scriptStruct,layout,event.bytes,"TypeDamagesArray","StructProperty",-1,wrapper)){reason="typed damage array not present";return {};}
 auto wrapperType=Struct(wrapper,layout,"TypeDamageArray");if(!wrapperType){reason="typed damage array struct rejected";return {};}
 ReflectedDamage::Property array;
 if(!Property(wrapperType,layout,wrapper.size,"TypeDamages","ArrayProperty",16,array)){reason="typed damage elements not present";return {};}
 // FArrayProperty.Inner is also discovered by identity/type, not a fixed offset.
 ReflectedDamage::Property inner;uintptr_t elementType=0;int matches=0;
 for(uintptr_t offset=0x70;offset<=0xB0;offset+=8){
  auto at=Mem::ReadPtr(array.address+offset);if(!at || !Mem::Readable((void*)at,0x78))continue;
  ReflectedDamage::Property p{at,Mem::ReadI32(at+layout.offset,-1),Mem::ReadI32(at+layout.size,-1),DamageFields::Kind(at,layout)};
  auto type=Struct(p,layout,"TypeDamage");if(type && p.offset==0 && Mem::ReadI32(at+layout.size-4)==1){inner=p;elementType=type;++matches;}
 }
 if(matches!=1){reason="typed damage element layout missing or ambiguous";return {};}
 ReflectedDamage::Property amount,status;
 if(!Property(elementType,layout,inner.size,"Amount","FloatProperty",4,amount)) {reason="typed damage amount rejected";return {};}
 std::string propertyReason;
 if(!ReflectedDamage::FindProperty(elementType,layout,"StatusEffect",inner.size,status,propertyReason)){reason="status effect field not present";return {};}
 auto enumeration=Enum(status);std::map<uint8_t,std::string> names;
 if(!enumeration || !EnumNames(enumeration,names)){reason="status effect enum not validated";return {};}
 FUEArray values{};
 if(!Mem::Read(eventData+wrapper.offset+array.offset,values) || values.Num<0 || values.Num>64 || values.Max<values.Num || values.Max>1024 || (values.Num && (!values.Data || !Mem::Readable(values.Data,(size_t)values.Num*inner.size)))){reason="typed damage array storage rejected";return {};}
 std::set<std::string> causes;
 for(int i=0;i<values.Num;++i){auto at=(uintptr_t)values.Data+(size_t)i*inner.size;float damage=0;uint8_t value=0;
  if(!Mem::Read(at+amount.offset,damage) || !std::isfinite(damage) || damage<0 || !Mem::Read(at+status.offset,value)){reason="typed damage value rejected";return {};}
  if(damage==0)continue;
  auto named=names.find(value);if(named==names.end()){reason="unknown status effect value";return {};}
  auto cause=Label(named->second);if(cause.empty())cause=TagCause(at,elementType,inner.size,layout);
  if(cause.empty()){reason="positive damage has no specific status or damage-tag cause";return {};}
  causes.insert(cause);
 }
 if(causes.size()!=1){reason="missing or mixed status damage causes";return {};}
 reason="runtime-validated status effect and positive typed damage";return *causes.begin();
}
}
