#pragma once
#include "store.h"
#include "damage_fields.h"
namespace Thc::DeathTelemetry {
void Update(const std::vector<DomEngine::PlayerInfo>& players,const DamageFields::Layout& layout);
std::vector<Hit> Drain();
std::map<std::string,std::string> Bindings();
Json Status();
void Stop();
}
