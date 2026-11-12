#pragma once
#include "../vendor/nlohmann_json.h"
#include <string>
namespace Thc {
nlohmann::json SystemMetrics(const std::string& dataDirectory);
}
