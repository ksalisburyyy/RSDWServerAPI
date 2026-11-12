#pragma once
#include "../vendor/nlohmann_json.h"
#include <cmath>
#include <limits>
#include <string>
namespace Json {
inline std::string Escape(const std::string& s) {
    auto encoded=nlohmann::json(s).dump(-1,' ',false,nlohmann::json::error_handler_t::replace);
    return encoded.substr(1,encoded.size()-2);
}
inline nlohmann::json Object(const std::string& body) {
    auto value=nlohmann::json::parse(body,nullptr,false);
    return value.is_object()?value:nlohmann::json::object();
}
inline bool GetString(const std::string& body,const char* key,std::string& out) {
    auto value=Object(body);auto it=value.find(key);if(it==value.end() || !it->is_string())return false;
    out=it->get<std::string>();return true;
}
inline bool GetInt(const std::string& body,const char* key,int& out) {
    auto value=Object(body);auto it=value.find(key);if(it==value.end() || !it->is_number_integer())return false;
    if(it->is_number_unsigned()){auto n=it->get<uint64_t>();if(n>(uint64_t)std::numeric_limits<int>::max())return false;out=(int)n;}
    else {auto n=it->get<int64_t>();if(n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max())return false;out=(int)n;}
    return true;
}
inline bool GetFloat(const std::string& body,const char* key,float& out) {
    auto value=Object(body);auto it=value.find(key);if(it==value.end() || !it->is_number())return false;
    float n=it->get<float>();if(!std::isfinite(n))return false;out=n;return true;
}
inline bool GetBool(const std::string& body,const char* key,bool& out) {
    auto value=Object(body);auto it=value.find(key);if(it==value.end() || !it->is_boolean())return false;out=it->get<bool>();return true;
}
}
