#include "api_routes.h"
#include "../thc/service.h"
#include "../thc/event_page.h"
#include "dashboard.h"
#include "../engine/administration.h"
#include "../engine/world_save.h"
#include "../thc/access.h"
#include "../thc/control.h"
#include "../config/config.h"
#include "../discord/delivery.h"
#include <mutex>
#include <ctime>
#include <charconv>
#include <algorithm>
#include <array>
namespace ApiRoutes {
void RegisterThc(HttpServer& server) {
    server.Get("/dashboard",[](const HttpRequest&){HttpResponse r;r.contentType="text/html; charset=utf-8";r.body=kDashboard;return r;});
    server.Get("/api/thc/access/me",[](const HttpRequest& req){return JsonResponse(Thc::Json({{"role",req.role},{"id",req.accessId},{"label",req.accessLabel}}).dump());});
    server.Get("/api/thc/access",[](const HttpRequest&){return JsonResponse(Thc::Json({{"tokens",Thc::Access::Get().List()}}).dump());});
    server.Post("/api/thc/access/create",[](const HttpRequest& req){
        if(g_Config.rest.bearerToken.empty())return Error(400,"Configure the owner bearer token first");
        auto body=Json::Object(req.body);auto result=Thc::Access::Get().Create(body.value("label",std::string{}),body.value("role",std::string{}));
        return result.contains("error")?Error(400,result["error"]):JsonResponse(result.dump());
    });
    server.Post("/api/thc/access/revoke",[](const HttpRequest& req){auto body=Json::Object(req.body);return Thc::Access::Get().Revoke(body.value("id",std::string{}))?Ok("access revoked"):Error(404,"token not found or save failed");});
    server.Post("/api/thc/teleport",[](const HttpRequest& req){
        auto b=Json::Object(req.body);
        if(!b.contains("player") || !b["player"].is_string() || !b.contains("x") || !b["x"].is_number() || !b.contains("y") || !b["y"].is_number() || !b.contains("z") || !b["z"].is_number())return Error(400,"player and numeric x/y/z required");
        std::string result;return Administration::Teleport(b["player"],b["x"],b["y"],b["z"],result)?Ok(result):Error(503,result);
    });
    server.Post("/api/thc/console",[](const HttpRequest& req){std::string command,result;if(!Json::GetString(req.body,"command",command))return Error(400,"command required");return Administration::Console(command,result)?Ok(result):Error(503,result);});
    server.Get("/api/thc/functions",[](const HttpRequest& req){
        auto q=req.query.find("search");if(q==req.query.end() || q->second.size()<3 || q->second.size()>80)return Error(400,"search must contain 3 to 80 characters");
        int after=0;auto a=req.query.find("after");if(a!=req.query.end()){auto value=std::from_chars(a->second.data(),a->second.data()+a->second.size(),after);if(value.ec!=std::errc{} || value.ptr!=a->second.data()+a->second.size() || after<0)return Error(400,"invalid function cursor");}
        return JsonResponse(Administration::Functions(q->second,after).dump());
    });
    server.Get("/api/thc/save",[](const HttpRequest&){return JsonResponse(WorldSave::Status().dump());});
    server.Post("/api/thc/save",[](const HttpRequest&){
        if(WorldSave::Available()){auto result=WorldSave::Request();if(result.contains("error"))return Error(409,result["error"]);auto response=JsonResponse(result.dump());response.status=202;return response;}
        APIConfig saved=g_Config;saved.Load(APIConfig::GetConfigPath());
        if(saved.worldSaveCommand.empty())return Error(503,"Native save is not ready (recent game-thread activity is required), and no verified console fallback is configured; no save was attempted");
        std::string result;return Administration::Console(saved.worldSaveCommand,result)?Ok("configured save command invoked; completion must be confirmed in the game log"):Error(503,result);
    });
    server.Get("/api/thc/control",[](const HttpRequest&){return JsonResponse(Thc::ControlRequest({{"action","status"}}).dump());});
    for(auto action:std::array<std::pair<const char*,const char*>,3>{{{"/api/thc/control/logs","logs"},{"/api/thc/control/backups","backups"},{"/api/thc/control/game-settings","game_settings"}}})
        server.Get(action.first,[verb=std::string(action.second)](const HttpRequest&){auto result=Thc::ControlRequest({{"action",verb}});return result.value("ok",false)?JsonResponse(result.dump()):Error(503,result.value("error",std::string("control service unavailable")));});
    server.Post("/api/thc/control",[](const HttpRequest& req){
        auto body=Json::Object(req.body);if(!body.contains("action") || !body["action"].is_string())return Error(400,"action required");
        body["_auditActor"]={{"id",req.accessId},{"label",req.accessLabel},{"role",req.role}};
        auto result=Thc::ControlRequest(body);return result.value("ok",false)?JsonResponse(result.dump()):Error(409,result.value("error",std::string("control request failed")));
    });
    server.Get("/api/thc/settings",[](const HttpRequest&){
        APIConfig saved=g_Config;saved.Load(APIConfig::GetConfigPath());const auto& d=saved.discord;
        return JsonResponse(Thc::Json({{"apiPort",saved.rest.port},{"bindAddress",saved.rest.bindAddress},{"combatTracking",saved.combatTracking},
           {"saveCommand",saved.worldSaveCommand},{"discordEnabled",d.enabled},{"discordUsername",d.username},
           {"threadIds",{{"chat",d.chatThreadId.empty()?Discord::WebhookThread(d.webhookUrl):d.chatThreadId},{"deaths",d.deathThreadId.empty()?Discord::WebhookThread(d.deathWebhookUrl):d.deathThreadId},{"presence",d.presenceThreadId.empty()?Discord::WebhookThread(d.presenceWebhookUrl):d.presenceThreadId},{"status",d.statusThreadId.empty()?Discord::WebhookThread(d.statusWebhookUrl):d.statusThreadId},{"admin",d.adminThreadId.empty()?Discord::WebhookThread(d.adminWebhookUrl):d.adminThreadId}}},
           {"webhooks",{{"chat",!d.webhookUrl.empty()},{"deaths",!d.deathWebhookUrl.empty()},{"presence",!d.presenceWebhookUrl.empty()},{"status",!d.statusWebhookUrl.empty()},{"admin",!d.adminWebhookUrl.empty()}}}}).dump());
    });
    server.Post("/api/thc/settings",[](const HttpRequest& req){
        static std::mutex settingMutex;std::lock_guard<std::mutex> lock(settingMutex);
        APIConfig saved=g_Config;saved.Load(APIConfig::GetConfigPath());auto b=Json::Object(req.body);
        if(b.contains("apiPort")){if(!b["apiPort"].is_number_integer())return Error(400,"invalid port");int port=b["apiPort"];if(port<1024 || port>65535)return Error(400,"port must be 1024 to 65535");saved.rest.port=port;}
        if(b.contains("bindAddress") && b["bindAddress"]!="127.0.0.1")return Error(400,"web settings retain localhost-only binding");
        for(auto name:{"combatTracking","discordEnabled"})if(b.contains(name) && !b[name].is_boolean())return Error(400,"boolean setting required");
        if(b.contains("combatTracking"))saved.combatTracking=b["combatTracking"];
        if(b.contains("discordEnabled"))saved.discord.enabled=b["discordEnabled"];
        for(auto name:{"saveCommand","discordUsername"})if(b.contains(name) && (!b[name].is_string() || b[name].get<std::string>().size()>1024 || b[name].get<std::string>().find_first_of("\r\n")!=std::string::npos))return Error(400,"invalid text setting");
        if(b.contains("saveCommand"))saved.worldSaveCommand=b["saveCommand"];
        if(b.contains("discordUsername"))saved.discord.username=b["discordUsername"];
        for(auto pair:std::array<std::pair<const char*,std::string*>,5>{{{"chatWebhook",&saved.discord.webhookUrl},{"deathWebhook",&saved.discord.deathWebhookUrl},{"presenceWebhook",&saved.discord.presenceWebhookUrl},{"statusWebhook",&saved.discord.statusWebhookUrl},{"adminWebhook",&saved.discord.adminWebhookUrl}}}){
            if(!b.contains(pair.first))continue;
            if(!b[pair.first].is_string())return Error(400,"invalid webhook");
            auto value=b[pair.first].get<std::string>();
            if(!value.empty() && !Discord::ValidWebhook(value))return Error(400,"expected a Discord HTTPS webhook URL");
            *pair.second=value;
        }
        for(auto pair:std::array<std::pair<const char*,std::string*>,5>{{{"chatThreadId",&saved.discord.chatThreadId},{"deathThreadId",&saved.discord.deathThreadId},{"presenceThreadId",&saved.discord.presenceThreadId},{"statusThreadId",&saved.discord.statusThreadId},{"adminThreadId",&saved.discord.adminThreadId}}}){
            if(!b.contains(pair.first))continue;
            if(!b[pair.first].is_string() || !Discord::ValidThreadId(b[pair.first].get<std::string>()))return Error(400,"thread ID must be an exact Discord ID string or empty");
            *pair.second=b[pair.first].get<std::string>();
        }
        if(!saved.Save(APIConfig::GetConfigPath()))return Error(500,"configuration save failed; original preserved");
        return JsonResponse(Thc::Json({{"status","ok"},{"message","settings saved; restart required to apply listener/Discord changes"},{"restartRequired",true}}).dump());
    });
    server.Get("/api/thc/analytics",[](const HttpRequest&){
        auto data=Thc::Data();auto hours=Thc::Json::array();int64_t start=(time(nullptr)/3600-23)*3600;
        for(int i=0;i<24;++i)hours.push_back({{"time",start+i*3600},{"joins",0},{"deaths",0},{"kills",0},{"chat",0}});
        for(const auto& event:data["events"]){int64_t at=event["time"];if(at<start || at>=start+24*3600)continue;auto index=(at-start)/3600;std::string type=event["type"],key=type=="join"?"joins":type=="death"?"deaths":type=="kill"?"kills":type=="chat"?"chat":"";if(!key.empty())hours[index][key]=hours[index][key].get<int>()+1;}
        return JsonResponse(Thc::Json({{"hours",hours},{"source","retained THC events; older events may have expired"}}).dump());
    });

    server.Post("/api/thc/recheck",[](const HttpRequest&){
        if(!EngineReady())return Error(503,"Game engine is not ready; discovery retries automatically during startup");
        auto response=JsonResponse(Thc::RequestRecovery().dump());response.status=202;return response;
    });
    server.Get("/api/thc/status",[](const HttpRequest&){return JsonResponse(Thc::ServiceStatus().dump());});
    server.Get("/api/thc/characters",[](const HttpRequest& req){
        auto state=Thc::Data();auto result=Thc::Json::array();
        auto guid=req.query.find("guid");
        for(auto it=state["characters"].begin();it!=state["characters"].end();++it){
            if(guid!=req.query.end() && guid->second!=it.key())continue;
            auto row=it.value();row["online"]=std::find(state["onlineGuids"].begin(),state["onlineGuids"].end(),it.key())!=state["onlineGuids"].end();result.push_back(std::move(row));
        }
        return JsonResponse(Thc::Json({{"count",result.size()},{"characters",result}}).dump());
    });
    server.Get("/api/thc/events",[](const HttpRequest& req){
        uint64_t after=0,limit=100;
        for(auto entry:std::array<std::pair<const char*,uint64_t*>,2>{{{"after",&after},{"limit",&limit}}}) {
            auto it=req.query.find(entry.first);if(it==req.query.end())continue;
            auto parsed=std::from_chars(it->second.data(),it->second.data()+it->second.size(),*entry.second);
            if(parsed.ec!=std::errc{} || parsed.ptr!=it->second.data()+it->second.size())return Error(400,"invalid event cursor or limit");
        }
        if(limit==0 || limit>500)return Error(400,"limit must be between 1 and 500");
        bool latest=false;
        auto mode=req.query.find("latest");
        if(mode!=req.query.end()){
            if(mode->second!="1" && mode->second!="true" && mode->second!="0" && mode->second!="false")return Error(400,"latest must be true or false");
            latest=mode->second=="1" || mode->second=="true";
        }
        if(latest && req.query.count("after"))return Error(400,"latest and after cannot be combined");
        return JsonResponse(Thc::EventPage(Thc::Data(),after,(size_t)limit,latest).dump());
    });
}
}
