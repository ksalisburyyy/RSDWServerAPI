#include "thc/combat.cpp"
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
std::vector<std::pair<uintptr_t,size_t>> regions;
void Register(void* p,size_t n){regions.push_back({(uintptr_t)p,n});}
std::vector<std::string> names={"None"};
uint32_t Name(const std::string& text){for(size_t i=0;i<names.size();++i)if(names[i]==text)return (uint32_t)i;names.push_back(text);return (uint32_t)names.size()-1;}
alignas(uintptr_t) unsigned char anchor[0xE0]{},anchorFields[6][0x90]{},prediction[0xE0]{},predictFields[22][0x90]{},healthFn[0xE0]{},healthFields[2][0x90]{},classes[10][0x30]{};
alignas(uintptr_t) unsigned char playerActor[0x1900]{},goblinActor[0x1900]{},healthComponent[0x200]{},damageComponent[0x100]{},unknownActor[0x1900]{},aiController[0x400]{};
alignas(uintptr_t) unsigned char frame[0x100]{},params[736]{};
int originalCalls=0;float amount=8;
void GenericOriginal(void*,void*,void* result){if(result)*(float*)result=amount;}
void PredictOriginal(void*,void*,void* result){++originalCalls;if(result)*(float*)result=amount;}
void HealthOriginal(void*,void*,void*){++originalCalls;*(float*)(healthComponent+DomEngine::Offsets::Health_AuthoritativeHealth)=0;}
uintptr_t Class(const std::string& name){for(int i=0;i<10;++i){if(*(uint32_t*)classes[i]==Name(name))return (uintptr_t)classes[i];if(!*(uint32_t*)classes[i]){*(uint32_t*)classes[i]=Name(name);return (uintptr_t)classes[i];}}assert(false);return 0;}
void Field(unsigned char* address,const std::string& name,int offset,int size,const char* kind,unsigned char* next){*(uint32_t*)(address+0x28)=Name(name);*(uintptr_t*)(address+0x20)=(uintptr_t)next;*(int32_t*)(address+0x4C)=offset;*(int32_t*)(address+0x3C)=size;*(int32_t*)(address+0x38)=1;*(uintptr_t*)(address+8)=Class(kind);}
void Health(float value){*(float*)(healthComponent+DomEngine::Offsets::Health_AuthoritativeHealth)=value;}
}
namespace Mem {
bool Readable(const void* pointer,size_t size){auto p=(uintptr_t)pointer;for(auto r:regions)if(p>=r.first && p-r.first<=r.second && size<=r.second-(p-r.first))return true;return false;}
bool InImage(const void* p){return p==(void*)&GenericOriginal || p==(void*)&PredictOriginal || p==(void*)&HealthOriginal;}
}
namespace GameThread {bool FrameLayoutReady(){return true;}uintptr_t FrameNodeOffset(){return 0x10;}uintptr_t FrameLocalsOffset(){return 0x28;}}
namespace DomEngine {
Engine* g_Engine=nullptr;
std::string Engine::NameToString(uint32_t id,uint32_t)const{return id<names.size()?names[id]:"";}
uintptr_t Engine::FindFunction(const char* owner,const char* name)const{if(std::string(owner)=="GameplayStatics" && std::string(name)=="ApplyDamage")return (uintptr_t)anchor;if(std::string(owner)=="DamageComponent" && std::string(name)=="BP_PredictPointDamage")return (uintptr_t)prediction;if(std::string(owner)=="HealthComponent" && std::string(name)=="SetHealth")return (uintptr_t)healthFn;return 0;}
bool Engine::IsA(uintptr_t object,const char* name)const{std::string kind=name;if(object==(uintptr_t)playerActor)return kind=="Actor"||kind=="Pawn"||kind=="DominionPlayerCharacter";if(object==(uintptr_t)goblinActor || object==(uintptr_t)unknownActor)return kind=="Actor"||kind=="Pawn"||kind=="DominionAICharacter";if(object==(uintptr_t)aiController)return kind=="Controller";if(object==(uintptr_t)healthComponent)return kind=="HealthComponent";if(object==(uintptr_t)damageComponent)return kind=="DamageComponent";return false;}
std::string Engine::GetObjectClassName(uintptr_t object)const{return object==(uintptr_t)goblinActor?"BP_AI_MeleeGoblin_Character_C":object==(uintptr_t)unknownActor?"BP_UnmappedCreature_C":"";}
std::string Engine::GetObjectName(uintptr_t)const{return "";}
std::string Engine::ReadFString(uintptr_t address)const{FUEArray text{};if(!Mem::Read(address,text) || text.Num<1 || !Mem::Readable(text.Data,(size_t)text.Num*2))return {};std::string out;auto data=(const char16_t*)text.Data;for(int i=0;i<text.Num-1;++i)out+=(char)data[i];return out;}
}
int main(){
 for(auto region:{std::pair<void*,size_t>{anchor,sizeof(anchor)},{anchorFields,sizeof(anchorFields)},{prediction,sizeof(prediction)},{predictFields,sizeof(predictFields)},{healthFn,sizeof(healthFn)},{healthFields,sizeof(healthFields)},{classes,sizeof(classes)},{playerActor,sizeof(playerActor)},{goblinActor,sizeof(goblinActor)},{healthComponent,sizeof(healthComponent)},{damageComponent,sizeof(damageComponent)},{unknownActor,sizeof(unknownActor)},{aiController,sizeof(aiController)},{frame,sizeof(frame)},{params,sizeof(params)}})Register(region.first,region.second);
 DomEngine::Engine engine;DomEngine::g_Engine=&engine;
 const char* anchorNames[]={"DamagedActor","BaseDamage","EventInstigator","DamageCauser","DamageTypeClass","ReturnValue"};int offsets[]={0,8,16,24,32,40};
 for(int i=0;i<6;++i)Field(anchorFields[i],anchorNames[i],offsets[i],i==1||i==5?4:8,i==1||i==5?"FloatProperty":i==4?"ClassProperty":"ObjectProperty",i<5?anchorFields[i+1]:nullptr);
 *(uint32_t*)(anchor+0xB0)=0x400;*(uint32_t*)(anchor+0xB4)=(44u<<16)|6;*(uintptr_t*)(anchor+0x50)=(uintptr_t)anchorFields;*(uintptr_t*)(anchor+0xD8)=(uintptr_t)&GenericOriginal;
 const char* fields[]={"DamageClass","TypeDamages","ApplicationTime","Instigator","Source","Location","Direction","Directionality","Force","OptionalEffects","DamageProperties","AttackProperties","AttackId","StaminaCostMultiplier","DamageShape","DamageShapeTransform","bCanCauseCrits","ExtraA","ExtraB","ExtraC","ExtraD","ReturnValue"};
 int poff[]={0,8,24,32,40,48,72,96,100,104,120,124,128,132,136,144,240,241,242,243,248,728};int psz[]={1,16,4,8,8,24,24,1,4,16,4,4,4,4,8,96,1,1,1,1,16,4};
 for(int i=0;i<22;++i){const char* kind=i==3||i==4||i==14?"ObjectProperty":psz[i]==4?"FloatProperty":psz[i]==1?"ByteProperty":psz[i]==16?"ArrayProperty":"StructProperty";Field(predictFields[i],fields[i],poff[i],psz[i],kind,i<21?predictFields[i+1]:nullptr);}
 *(uint32_t*)(prediction+0xB0)=0x400;*(uint32_t*)(prediction+0xB4)=(736u<<16)|22;*(uintptr_t*)(prediction+0x50)=(uintptr_t)predictFields;*(uintptr_t*)(prediction+0xD8)=(uintptr_t)&PredictOriginal;
 Field(healthFields[0],"Value",0,4,"FloatProperty",healthFields[1]);Field(healthFields[1],"Context",8,16,"StrProperty",nullptr);
 *(uint32_t*)(healthFn+0xB0)=0x400;*(uint32_t*)(healthFn+0xB4)=(24u<<16)|2;*(uintptr_t*)(healthFn+0x50)=(uintptr_t)healthFields;*(uintptr_t*)(healthFn+0xD8)=(uintptr_t)&HealthOriginal;
 *(int32_t*)(playerActor+DomEngine::Offsets::UObject_Index)=42;*(uintptr_t*)(playerActor+DomEngine::Offsets::DomPlayerChar_HealthComponent)=(uintptr_t)healthComponent;*(uintptr_t*)(healthComponent+DomEngine::Offsets::UObject_Outer)=(uintptr_t)playerActor;*(float*)(healthComponent+DomEngine::Offsets::Health_MaxHealth)=100;Health(8);*(uintptr_t*)(damageComponent+DomEngine::Offsets::UObject_Outer)=(uintptr_t)playerActor;
 *(uintptr_t*)(predictFields[3]+8)=Class("ClassProperty");
 Thc::Combat::Poll();assert(!Thc::Combat::Status()["playerDamagePredictionHook"].get<bool>());
 assert(Thc::Combat::Status()["predictionDiagnostic"]["state"]=="required field type rejected");
 assert(Thc::Combat::Status()["predictionDiagnostic"]["fields"].size()==22);
 *(uintptr_t*)(predictFields[3]+8)=Class("ObjectProperty");
 Thc::Combat::Poll();assert(Thc::Combat::Status()["typedFieldLayoutValidated"]==true);assert(Thc::Combat::Status()["playerDamagePredictionHook"]==true);assert(Thc::Combat::Status()["healthContextHooks"]==1);
 *(uintptr_t*)(frame+0x10)=(uintptr_t)prediction;*(uintptr_t*)(frame+0x28)=(uintptr_t)params;*(uintptr_t*)(params+32)=(uintptr_t)goblinActor;
 float result=0;Register(&result,sizeof(result));auto call=(void(*)(void*,void*,void*))(*(uintptr_t*)(prediction+0xD8));
 call(damageComponent,frame,&result);assert(originalCalls==1 && result==8);
 DomEngine::PlayerInfo p;p.pawnPtr=(uintptr_t)playerActor;p.characterGuid=std::string(32,'A');p.characterName="Callmebury";p.hasHealth=true;p.health=0;p.isDead=true;
 Health(0);auto now=Thc::Combat::Now();auto hits=Thc::Combat::Drain({p},now);assert(hits.size()==1 && !hits[0].fatalConfirmed && !hits[0].causeSettled);
 // Ambiguous lethal predictions must not blame the one known creature.
 Health(8);call(damageComponent,frame,&result);*(uintptr_t*)(params+32)=(uintptr_t)unknownActor;call(damageComponent,frame,&result);
 Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].causeSettled);
 // Resolve a non-player controller only with its reciprocal pawn link.
 *(uintptr_t*)(aiController+DomEngine::Offsets::Controller_Pawn)=(uintptr_t)goblinActor;*(uintptr_t*)(goblinActor+DomEngine::Offsets::Pawn_Controller)=(uintptr_t)aiController;
 *(uintptr_t*)(params+32)=(uintptr_t)aiController;Health(8);call(damageComponent,frame,&result);Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].causeSettled && !hits[0].fatalConfirmed);
 *(uintptr_t*)(goblinActor+DomEngine::Offsets::Pawn_Controller)=0;Health(8);call(damageComponent,frame,&result);Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].causeSettled);
 // Source pointer reuse cannot credit a replacement actor.
 *(uintptr_t*)(params+32)=(uintptr_t)goblinActor;Health(8);call(damageComponent,frame,&result);*(int32_t*)(goblinActor+DomEngine::Offsets::UObject_Index)=99;
 Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].causeSettled);
 // Prediction without actual health loss cannot become a hit/death attribution.
 Health(8);p.health=8;p.isDead=false;call(damageComponent,frame,&result);now=Thc::Combat::Now();assert(Thc::Combat::Drain({p},now).empty());assert(Thc::Combat::Drain({p},now+3).empty());
 // A nonlethal prediction must not settle a later larger/unobserved fatal loss.
 Health(100);call(damageComponent,frame,&result);p.health=0;p.isDead=true;Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].fatalConfirmed && !hits[0].causeSettled);
 // Explicit health contexts require a real positive-to-zero change.
 Health(8);char16_t text[]=u"Fall";Register(text,sizeof(text));FUEArray context{(void*)text,5,5};std::memcpy(params+8,&context,sizeof(context));*(uintptr_t*)(frame+0x10)=(uintptr_t)healthFn;
 auto healthCall=(void(*)(void*,void*,void*))(*(uintptr_t*)(healthFn+0xD8));healthCall(healthComponent,frame,nullptr);Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && hits[0].cause=="Fall damage" && hits[0].causeSettled);
 // Keep an actual fatal witness until the player attribute snapshot catches up.
 Health(8);p.health=8;p.isDead=false;healthCall(healthComponent,frame,nullptr);now=Thc::Combat::Now();assert(Thc::Combat::Drain({p},now).empty());
 p.health=0;p.isDead=true;hits=Thc::Combat::Drain({p},now+0.1);assert(hits.size()==1 && hits[0].cause=="Fall damage" && hits[0].causeSettled);
 Health(8);char16_t suicide[]=u"Suicide";Register(suicide,sizeof(suicide));context={suicide,8,8};std::memcpy(params+8,&context,sizeof(context));healthCall(healthComponent,frame,nullptr);Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && hits[0].cause=="Suicide" && hits[0].causeSettled);
 Health(8);char16_t unknown[]=u"Unclassified health update";Register(unknown,sizeof(unknown));context={unknown,(int32_t)(sizeof(unknown)/2),(int32_t)(sizeof(unknown)/2)};std::memcpy(params+8,&context,sizeof(context));healthCall(healthComponent,frame,nullptr);Health(0);hits=Thc::Combat::Drain({p},Thc::Combat::Now());assert(hits.size()==1 && !hits[0].causeSettled);
 // Parameter-count and network-function drift cannot pass the local witness gate.
 std::map<std::string,Thc::DamageFields::Field> drift;
 *(uint32_t*)(prediction+0xB4)=(736u<<16)|21;assert(!Thc::DamageFields::Read((uintptr_t)prediction,Thc::Combat::typedLayout,736,22,drift));
 *(uint32_t*)(prediction+0xB4)=(736u<<16)|22;*(uint32_t*)(prediction+0xB0)=0x440;assert(!Thc::DamageFields::Read((uintptr_t)prediction,Thc::Combat::typedLayout,736,22,drift));*(uint32_t*)(prediction+0xB0)=0x400;
 // Type mismatch and wrong native frame are rejected without swallowing calls.
 auto bad=Thc::Combat::typedLayout;std::map<std::string,Thc::DamageFields::Field> map;assert(Thc::DamageFields::Read((uintptr_t)prediction,bad,736,22,map));*(uintptr_t*)(predictFields[3]+8)=Class("ClassProperty");assert(Thc::DamageFields::Read((uintptr_t)prediction,bad,736,22,map));assert(!Thc::DamageFields::Is(map,"Instigator","ObjectProperty",8));
 *(uintptr_t*)(frame+0x10)=0;int before=originalCalls;call(damageComponent,frame,&result);assert(originalCalls==before+1 && Thc::Combat::Drain({p},Thc::Combat::Now()).empty());
 assert(Thc::Combat::EnvironmentalCause("BP_FallenKnight_C").empty());
 std::cout<<"PASS: typed 736/22 prediction gate, prediction-only refusal, evade/nonlethal refusal, explicit fall/suicide contexts, unknown context refusal, field-type checks and original forwarding\n";
}
