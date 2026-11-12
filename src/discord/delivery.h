#pragma once
#include "../vendor/nlohmann_json.h"
#include <functional>
#include <mutex>
#include <string>
namespace Discord {
using Json=nlohmann::json;
struct Response { int status=0; int retryAfter=0; };
class DeliveryQueue {
    mutable std::mutex mutex;
    std::mutex processing;
    std::string path;
    Json state;
    std::string error;
    int lastHttp=0;
    bool ready=false;
    bool Persist(const Json& candidate);
public:
    explicit DeliveryQueue(std::string path);
    bool Load();
    bool Enqueue(const std::string& content);
    bool Step(int64_t now,const std::function<Response(const std::string&)>& transport);
    Json Status() const;
};
Response CurlPost(const std::string& url,const std::string& payload);
bool ValidWebhook(const std::string& url);
bool ValidThreadId(const std::string& id);
std::string RouteWebhook(const std::string& url,const std::string& thread);
std::string WebhookThread(const std::string& url);
}
