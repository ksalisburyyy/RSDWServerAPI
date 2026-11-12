#pragma once
#include "dom_engine.h"
#include <cmath>
namespace DomEngine {
inline void ApplyHealthAttributes(PlayerInfo& info){
 info.hasHealth=false;info.health=0;info.maxHealth=0;info.isDead=false;
 for(const auto& attribute:info.attributes){
  if(!std::isfinite(attribute.value))continue;
  if(attribute.name=="Health"){info.health=attribute.value;info.hasHealth=true;}
  else if(attribute.name=="MaxHealth" && attribute.value>0)info.maxHealth=attribute.value;
 }
 // MaxHealth alone is not evidence of current HP or a death.
 info.isDead=info.hasHealth && info.health<=0;
}
}
