#include "discord/delivery.h"
#include "config/config.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
using Discord::Json;
int main(){
 char base[]="/tmp/thc-delivery-XXXXXX";assert(mkdtemp(base));std::string path=std::string(base)+"/outbox.json";
 Discord::DeliveryQueue queue(path);assert(queue.Load());assert(queue.Enqueue("Hello"));int calls=0;
 assert(queue.Step(100,[&](const std::string& text){++calls;assert(text=="Hello");return Discord::Response{429,10};}));
 assert(!queue.Step(109,[&](const std::string&){++calls;return Discord::Response{204,0};}));assert(calls==1);
 Discord::DeliveryQueue restart(path);assert(restart.Load());assert(restart.Step(111,[&](const std::string& text){++calls;assert(text=="Hello");return Discord::Response{204,0};}));assert(calls==2);assert(restart.Status()["pending"]==0);assert(restart.Status()["sent"]==1);
 assert(restart.Enqueue("Rejected"));assert(restart.Step(120,[](const std::string&){return Discord::Response{403,0};}));assert(restart.Status()["failed"]==1 && restart.Status()["pending"]==0);
 std::string big(1899,'a');big+="🌍";assert(restart.Enqueue(big));assert(restart.Status()["pending"]==2);
 for(int i=0;i<2;++i)assert(restart.Step(130+i,[](const std::string& text){assert(text.size()<=1900);assert(Json::parse(Json(text).dump()).is_string());return Discord::Response{204,0};}));
 assert(restart.Enqueue("network failure"));restart.Step(150,[](const std::string&){return Discord::Response{};});assert(restart.Status()["pending"]==1);
 // Enqueue failure leaves the already-persisted queue intact.
 std::filesystem::rename(base,std::string(base)+"-moved");assert(!restart.Enqueue("not committed"));assert(restart.Status()["pending"]==1);std::filesystem::rename(std::string(base)+"-moved",base);
 assert(Discord::ValidWebhook("https://discord.com/api/webhooks/123/token?thread_id=456&wait=true"));
 assert(!Discord::ValidWebhook("https://discord.com.evil/api/webhooks/123/token"));assert(!Discord::ValidWebhook("http://discord.com/api/webhooks/123/token"));
 const std::string thread="1560963849358934087",baseUrl="https://discord.com/api/webhooks/123/token";
 assert(Discord::ValidThreadId(thread) && !Discord::ValidThreadId("1560963849358934087.0") && !Discord::ValidThreadId("18446744073709551616"));
 const auto originalUrl=baseUrl+"?thread_id=1560961724671332372&wait=true";
 assert(Discord::RouteWebhook(originalUrl,"")==originalUrl);
 assert(Discord::RouteWebhook(originalUrl,thread)==baseUrl+"?wait=true&thread_id="+thread);
 assert(Discord::WebhookThread(Discord::RouteWebhook(originalUrl,thread))==thread);
 assert(Discord::RouteWebhook(baseUrl,"not-an-id").empty());
 assert(Discord::RouteWebhook(baseUrl+"?thread_id=1&thread_id=2",thread)==baseUrl+"?thread_id="+thread);
 APIConfig config;config.rest.bearerToken="fixture-owner";config.discord.webhookUrl=originalUrl;config.discord.adminThreadId=thread;
 auto configPath=std::string(base)+"/settings.ini";assert(config.Save(configPath));APIConfig loaded;assert(loaded.Load(configPath));assert(loaded.discord.adminThreadId==thread && loaded.discord.webhookUrl==originalUrl);
 // Fake curl: no network; prove stdin payload, thread query preservation and
 // removal of preload variables from child environments.
 std::string script=std::string(base)+"/curl";std::ofstream fake(script);
 fake<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$THC_TEST_ARGS\"\n[ -z \"$LD_PRELOAD\" ] || exit 31\n[ -z \"$LD_AUDIT\" ] || exit 32\ncat > \"$THC_TEST_CAPTURE\"\nwhile [ $# -gt 0 ]; do\nif [ \"$1\" = --dump-header ]; then shift; printf 'HTTP/1.1 429 Too Many Requests\\r\\nRetry-After: 7\\r\\n' > \"$1\"; fi\nshift\ndone\nprintf 429\n";fake.close();chmod(script.c_str(),0700);
 std::string previous=getenv("PATH")?getenv("PATH"):"";std::string custom=std::string(base)+":"+previous;
 setenv("PATH",custom.c_str(),1);setenv("LD_PRELOAD","/does-not-exist.so",1);setenv("LD_AUDIT","/does-not-exist.so",1);
 auto capture=std::string(base)+"/capture.json";setenv("THC_TEST_CAPTURE",capture.c_str(),1);
 auto argsPath=std::string(base)+"/args.txt";setenv("THC_TEST_ARGS",argsPath.c_str(),1);
 const std::string destination="https://discord.com/api/webhooks/123/test?thread_id=1560963849358934087&wait=true";
 auto response=Discord::CurlPost(destination,"{\"content\":\"hello\"}");
 unsetenv("LD_PRELOAD");unsetenv("LD_AUDIT");setenv("PATH",previous.c_str(),1);
 std::ifstream arguments(argsPath);std::string argument;bool foundDestination=false;
 while(std::getline(arguments,argument))if(argument==destination)foundDestination=true;
 assert(foundDestination);unsetenv("THC_TEST_ARGS");
 assert(response.status==429 && response.retryAfter==7);std::ifstream payload(capture);Json body;payload>>body;assert(body["content"]=="hello");
 std::filesystem::remove_all(base);
 std::cout<<"PASS: durable retry, Retry-After, restart replay, permanent rejection, Unicode splitting, network failure, write rollback and exact 19-digit thread query preservation and shell-free/preload-free fake curl transport\n";
}
