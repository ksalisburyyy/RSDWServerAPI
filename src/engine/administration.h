#pragma once
#include "../vendor/nlohmann_json.h"
#include <string>
namespace Administration {
void Discover();
nlohmann::json Capabilities();
nlohmann::json Functions(const std::string& search,int after);
bool Teleport(const std::string& player,double x,double y,double z,std::string& result);
bool Console(const std::string& command,std::string& result);
}
