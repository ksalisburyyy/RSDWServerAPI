#pragma once
#include "store.h"
#include "notification_style.h"
#include <algorithm>
#include <cctype>
namespace Thc {
inline std::string AuditAction(const std::string& path){
 static const std::map<std::string,std::string> names={{"/api/give","Give item"},{"/api/kick","Kick player"},{"/api/ban","Ban player"},{"/api/unban","Unban player"},{"/api/broadcast","Broadcast"},{"/api/thc/teleport","Teleport player"},{"/api/thc/console","Run console command"},{"/api/thc/save","Save world"},{"/api/thc/settings","Change settings"},{"/api/thc/access/create","Create access token"},{"/api/thc/access/revoke","Revoke access token"},{"/api/thc/recheck","Compatibility check and recovery"}};
 auto found=names.find(path);return found==names.end()?path:found->second;
}
inline Json HttpAuditDetails(const std::string& path,const std::string& body,int status,const std::string& response,const Json& actor){
 auto input=Json::parse(body,nullptr,false);if(!input.is_object())input=Json::object();
 auto result=Json::parse(response,nullptr,false);Json fields=Json::object();
 const std::map<std::string,std::vector<std::string>> allowed={{"/api/give",{"player","item","count"}},{"/api/kick",{"player","reason"}},{"/api/ban",{"player","reason"}},{"/api/unban",{"player"}},{"/api/broadcast",{"sender","message"}},{"/api/thc/teleport",{"player","x","y","z"}}};
 auto names=allowed.find(path);if(names!=allowed.end())for(const auto& key:names->second)if(input.contains(key) && (input[key].is_string() || input[key].is_number())){
  fields[key]=input[key];if(fields[key].is_string())fields[key]=fields[key].get<std::string>().substr(0,1000);
 }
 if(path=="/api/thc/access/create" || path=="/api/thc/access/revoke")for(const auto* key:{"label","role","id"})if(input.contains(key) && input[key].is_string())fields[key]=input[key].get<std::string>().substr(0,100);
 if(path=="/api/thc/settings"){
  fields["changedFields"]=Json::array();for(const auto* key:{"apiPort","bindAddress","combatTracking","saveCommand","discordEnabled","discordUsername","chatWebhook","deathWebhook","presenceWebhook","statusWebhook","adminWebhook","chatThreadId","deathThreadId","presenceThreadId","statusThreadId","adminThreadId"})if(input.contains(key))fields["changedFields"].push_back(key);
 }
 if(path=="/api/thc/console" && input.contains("command") && input["command"].is_string()){
  auto command=input["command"].get<std::string>().substr(0,1024),lower=command;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return (char)tolower(c);});
  bool sensitive=false;for(const auto* word:{"password","token","secret","webhook","credential","auth"})if(lower.find(word)!=std::string::npos)sensitive=true;
  fields["command"]=sensitive?"[sensitive command arguments redacted]":command;
 }
 std::string message="HTTP "+std::to_string(status);
 // Access-create replies contain the new secret. Never put those replies in audits.
 if(names!=allowed.end() && result.is_object()){
  for(const auto* key:{"message","error"})if(result.contains(key) && result[key].is_string())message=result[key].get<std::string>().substr(0,1500);
 }
 return {{"action",AuditAction(path)},{"path",path},{"actor",actor},{"fields",fields},{"result",message},{"success",status>=200 && status<300},{"httpStatus",status}};
}
inline std::string AuditDisplay(std::string value){for(auto& c:value)if((unsigned char)c<32)c=' ';return DiscordName(value);}
inline std::string FormatHttpAudit(const Json& details){
 const auto actor=details.value("actor",Json::object());
 std::string text="**Action:** "+DiscordName(details.value("action",std::string{}))+"\n**Access:** "+AuditDisplay(actor.value("label",std::string("Unknown credential")))+" ("+AuditDisplay(actor.value("role",std::string{}))+")";
 text+="\n**Credential ID:** "+AuditDisplay(actor.value("id",std::string{}));
 text+="\n**Result:** "+DiscordName(details.value("result",std::string{}));
 const auto fields=details.value("fields",Json::object());
 for(auto it=fields.begin();it!=fields.end();++it){
  auto value=it->is_string()?it->get<std::string>():it->dump();
  auto key=it.key();if(!key.empty())key[0]=(char)toupper((unsigned char)key[0]);
  text+="\n**"+DiscordName(key)+":** "+DiscordName(value);
 }
 return text;
}
}
