#include "engine/health_attributes.h"
#include <cassert>
#include <limits>
#include <iostream>
int main(){DomEngine::PlayerInfo p;DomEngine::AttributeInfo value;
 value.name="MaxHealth";value.value=100;p.attributes={value};DomEngine::ApplyHealthAttributes(p);assert(!p.hasHealth && !p.isDead && p.maxHealth==100);
 value.name="Health";value.value=75;p.attributes.push_back(value);DomEngine::ApplyHealthAttributes(p);assert(p.hasHealth && !p.isDead && p.health==75);
 p.attributes.back().value=0;DomEngine::ApplyHealthAttributes(p);assert(p.hasHealth && p.isDead);
 p.attributes.back().value=std::numeric_limits<float>::quiet_NaN();DomEngine::ApplyHealthAttributes(p);assert(!p.hasHealth && !p.isDead);
 p.attributes.clear();DomEngine::ApplyHealthAttributes(p);assert(!p.hasHealth && !p.isDead && p.maxHealth==0);
 std::cout<<"PASS: MaxHealth-only and nonfinite/missing Health never imply death; real positive/zero health preserved\n";}
