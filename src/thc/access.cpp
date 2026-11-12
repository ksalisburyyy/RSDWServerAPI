#include "access.h"
#include "../config/config.h"
#include "../utils/atomic_file.h"
#include <filesystem>
#include <fstream>
namespace Thc::Access {
namespace {bool Equal(const std::string& a,const std::string& b){if(a.size()!=b.size())return false;unsigned char diff=0;for(size_t i=0;i<a.size();++i)diff|=(unsigned char)a[i]^(unsigned char)b[i];return diff==0;}}
bool Manager::Load(){std::lock_guard<std::mutex> lock(mutex);loaded=false;try{
 std::filesystem::create_directories(std::filesystem::path(path).parent_path());
 std::filesystem::permissions(std::filesystem::path(path).parent_path(),std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
 if(std::filesystem::exists(path)){
  if(std::filesystem::file_size(path)>128*1024)return false;
  std::ifstream file(path);nlohmann::json data;file>>data;
  if(!data.is_array() || data.size()>128)return false;
  for(auto& row:data)if(!row.at("id").is_string() || !row.at("token").is_string() || !row.at("label").is_string() ||
     (row.at("role")!="viewer" && row.at("role")!="operator" && row.at("role")!="admin"))return false;
  entries=std::move(data);
 }loaded=true;return true;
}catch(...){return false;}}
bool Manager::Save(const nlohmann::json& value){std::string error;return WriteFileAtomically(path,value.dump(),error);}
nlohmann::json Manager::Identity(const std::string& token,const std::string& owner)const{
 if(owner.empty())return {{"id","unauthenticated"},{"label","Unauthenticated API access"},{"role","admin"}};
 if(Equal(token,owner))return {{"id","owner"},{"label","Owner"},{"role","admin"}};
 std::lock_guard<std::mutex> lock(mutex);if(!loaded)return nlohmann::json::object();
 for(const auto& row:entries)if(Equal(token,row["token"]))return {{"id",row["id"]},{"label",row["label"]},{"role",row["role"]}};
 return nlohmann::json::object();
}
std::string Manager::Role(const std::string& token,const std::string& owner)const{return Identity(token,owner).value("role",std::string{});}
nlohmann::json Manager::List()const{std::lock_guard<std::mutex> lock(mutex);auto result=nlohmann::json::array();for(const auto& row:entries)result.push_back({{"id",row["id"]},{"label",row["label"]},{"role",row["role"]}});return result;}
nlohmann::json Manager::Create(const std::string& label,const std::string& role){
 std::lock_guard<std::mutex> lock(mutex);
 if(!loaded || entries.size()>=128 || label.empty() || label.size()>80 || (role!="viewer" && role!="operator" && role!="admin"))return {{"error","invalid label/role or access store unavailable"}};
 auto token=APIConfig::GenerateSecureToken(32),id=APIConfig::GenerateSecureToken(8);if(token.empty() || id.empty())return {{"error","secure random source unavailable"}};
 nlohmann::json row={{"id",id},{"label",label},{"role",role},{"token",token}};auto next=entries;next.push_back(row);
 if(!Save(next))return {{"error","could not persist access token"}};
 entries=std::move(next);return row;
}
bool Manager::Revoke(const std::string& id){std::lock_guard<std::mutex> lock(mutex);auto next=entries;for(auto it=next.begin();it!=next.end();++it)if((*it)["id"]==id){next.erase(it);if(!Save(next))return false;entries=std::move(next);return true;}return false;}
Manager& Get(){static Manager m(GetBaseDir()+"/thc/access.json");return m;}
bool Allows(const std::string& role,const std::string& method,const std::string& path){
 if(role=="admin")return true;
 if(role.empty())return false;
 if(path.rfind("/api/thc/access",0)==0)return path=="/api/thc/access/me";
 if(path.rfind("/api/thc/settings",0)==0 || path.rfind("/api/thc/control",0)==0 || path=="/api/thc/functions")return false;
 if(method=="GET")return true;
 return role=="operator" && (path=="/api/kick" || path=="/api/give" || path=="/api/broadcast" || path=="/api/thc/teleport");
}
}
