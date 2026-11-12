#pragma once
#include "../vendor/nlohmann_json.h"
namespace WorldSave {
void Initialize();
void Stop();
void Discover();
bool Available();
nlohmann::json Status();
nlohmann::json Request();
nlohmann::json TakeCompletion();
}
