#pragma once
#include "store.h"
#include "damage_fields.h"
#include <map>
namespace Thc::Combat {
struct Parameters { int size=0; std::map<std::string,int> fields; };
// Discover FField links against the known UE ApplyDamage argument layout.
// Any mismatch or ambiguity leaves the observer disarmed.
bool Discover(uintptr_t function, Parameters& parameters);
void Poll();
std::vector<Hit> Drain(const std::vector<DomEngine::PlayerInfo>& players,double now);
Json Status();
DamageFields::Layout ReflectionLayout();
void Stop();
}
