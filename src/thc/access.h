#pragma once
#include "../vendor/nlohmann_json.h"
#include <mutex>
#include <string>
namespace Thc::Access {
class Manager {
    std::string path;mutable std::mutex mutex;nlohmann::json entries=nlohmann::json::array();bool loaded=false;
    bool Save(const nlohmann::json& value);
public:
    explicit Manager(std::string file):path(std::move(file)){}
    bool Load();
    nlohmann::json Identity(const std::string& token,const std::string& ownerToken)const;
    std::string Role(const std::string& token,const std::string& ownerToken)const;
    nlohmann::json List()const;
    nlohmann::json Create(const std::string& label,const std::string& role);
    bool Revoke(const std::string& id);
};
Manager& Get();
bool Allows(const std::string& role,const std::string& method,const std::string& path);
}
