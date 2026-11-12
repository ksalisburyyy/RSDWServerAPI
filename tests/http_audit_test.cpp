#include "thc/http_audit.h"
#include <cassert>
#include <iostream>
int main(){Thc::Json actor={{"id","39466d24bfda5bb"},{"label","Minghoo"},{"role","admin"}};
 auto record=Thc::HttpAuditDetails("/api/give",R"({"player":"2533274996212391","item":"ITEM_Ammo_Bolts_Bronze","count":1,"actor":"spoofed"})",200,R"({"status":"ok","message":"gave 1 x bolts"})",actor);
 assert(record["actor"]["label"]=="Minghoo" && record["fields"]["count"]==1 && !record["fields"].contains("actor"));
 auto text=Thc::FormatHttpAudit(record);assert(text.find("Minghoo (admin)")!=std::string::npos && text.find("39466d24bfda5bb")!=std::string::npos && text.find("Give item")!=std::string::npos);
 record=Thc::HttpAuditDetails("/api/thc/access/create",R"({"label":"Minghoo","role":"admin"})",200,R"({"token":"PRIVATE-NEW-SECRET","id":"example"})",actor);
 assert(record.dump().find("PRIVATE-NEW-SECRET")==std::string::npos);
 record=Thc::HttpAuditDetails("/api/thc/settings",R"({"adminWebhook":"PRIVATE-WEBHOOK","discordEnabled":true})",200,"{}",actor);
 assert(record.dump().find("PRIVATE-WEBHOOK")==std::string::npos && record["fields"]["changedFields"].size()==2);
 record=Thc::HttpAuditDetails("/api/thc/console",R"({"command":"set password PRIVATE-PASSWORD"})",200,"{}",actor);
 assert(record.dump().find("PRIVATE-PASSWORD")==std::string::npos);
 record=Thc::HttpAuditDetails("/api/give","{}",403,R"({"error":"Denied"})",actor);assert(record["success"]==false && record["result"]=="Denied");
 assert(Thc::AuditDisplay("Minghoo\n**spoof**").find('\n')==std::string::npos);
 std::cout<<"PASS: labelled credential identity, stable ID, spoofed actor exclusion, secret-free token creation/settings/console and denied-action format\n";
}
