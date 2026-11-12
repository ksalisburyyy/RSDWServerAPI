#include "thc/access.h"
#include "thc/metrics.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){
 char directory[]="/tmp/thc-access-XXXXXX";assert(mkdtemp(directory));std::string path=std::string(directory)+"/access.json";
 Thc::Access::Manager access(path);assert(access.Load());assert(access.Role("owner","owner")=="admin");assert(access.Role("wrong","owner").empty());
 auto viewer=access.Create("Read only","viewer");assert(viewer.contains("token"));auto token=viewer["token"].get<std::string>();assert(access.Role(token,"owner")=="viewer");
 auto identity=access.Identity(token,"owner");assert(identity["label"]=="Read only" && identity["id"]==viewer["id"] && !identity.contains("token"));
 assert(access.Identity("owner","owner")["label"]=="Owner");assert(access.Identity("wrong","owner").empty());
 assert(access.List().dump().find(token)==std::string::npos);assert(!Thc::Access::Allows("viewer","POST","/api/kick"));assert(Thc::Access::Allows("viewer","GET","/api/players"));assert(!Thc::Access::Allows("viewer","GET","/api/thc/settings"));
 assert(Thc::Access::Allows("operator","POST","/api/kick"));assert(Thc::Access::Allows("operator","POST","/api/thc/teleport"));assert(!Thc::Access::Allows("operator","POST","/api/ban"));assert(!Thc::Access::Allows("operator","POST","/api/thc/control"));
 Thc::Access::Manager loaded(path);assert(loaded.Load());assert(loaded.Role(token,"owner")=="viewer");assert(loaded.Revoke(viewer["id"]));assert(loaded.Role(token,"owner").empty());
 auto metrics=Thc::SystemMetrics(directory);assert(metrics["diskTotalBytes"].is_number());assert(metrics["rssBytes"].is_number());assert(metrics["processUptimeSeconds"].is_number());
 std::filesystem::remove_all(directory);std::cout<<"PASS: access creation, roles, secret-free listings, persistence, revocation, permission matrix and real read-only system metrics\n";
}
