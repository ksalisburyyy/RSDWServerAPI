#pragma once
#include "../vendor/nlohmann_json.h"
#include <string>
namespace Discord {
inline std::string MessagePayload(const std::string& name,const std::string& content) {
    nlohmann::json payload={{"content",content},{"allowed_mentions",{{"parse",nlohmann::json::array()}}}};
    // Blank means use the webhook's configured Discord display name/avatar.
    if(!name.empty())payload["username"]=name;
    return payload.dump();
}
}
