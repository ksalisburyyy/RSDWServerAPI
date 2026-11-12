#include "message.h"
#include "webhook.h"
#include "delivery.h"
#include "../config/config.h"
#include "../utils/logger.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <memory>
#include <thread>
namespace Discord {
namespace {
class Sender {
    const std::string name;
    std::string url,username;
    std::atomic<bool> enabled{false},running{false};
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> incoming;
    std::unique_ptr<DeliveryQueue> outbox;
    std::atomic<uint64_t> refused{0};
public:
    explicit Sender(std::string label):name(std::move(label)){}
    void Start(const std::string& target,const std::string& user) {
        if(enabled || target.empty())return;
        if(!ValidWebhook(target)){LogMessage("Discord: "+name+" URL rejected (expected Discord HTTPS webhook)");return;}
        url=target;username=user;
        outbox=std::make_unique<DeliveryQueue>(GetBaseDir()+"/thc/outbox-"+name+".json");
        if(!outbox->Load()){LogMessage("Discord: "+name+" outbox could not be loaded; preserving original");return;}
        enabled=true;running=true;
        worker=std::thread([this]{
            try{Run();}catch(...){enabled=false;running=false;LogMessage("Discord: "+name+" worker stopped after an internal error");}
        });
        LogMessage("Discord: "+name+" webhook enabled");
    }
    void Stop(){running=false;wake.notify_all();if(worker.joinable())worker.join();enabled=false;}
    bool Enabled()const{return enabled;}
    bool Post(const std::string& line) {
        if(!enabled || line.empty())return false;
        // Hooks enqueue only: network and disk work belong to this worker.
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
        if(!lock.owns_lock()){++refused;return false;}
        if(incoming.size()>=512){++refused;return false;}
        incoming.push_back(line);wake.notify_all();return true;
    }
    Json Status()const {
        std::lock_guard<std::mutex> lock(mutex);
        Json result=outbox?outbox->Status():Json::object();
        result["enabled"]=enabled.load();result["buffered"]=incoming.size();result["refused"]=refused.load();return result;
    }
    void Run(){
        auto lastSend=std::chrono::steady_clock::time_point{};
        for(;;){
            std::deque<std::string> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::seconds(1),[this]{return !incoming.empty() || !running;});
                batch.swap(incoming);
            }
            std::deque<std::string> retry;
            for(const auto& line:batch)if(!outbox->Enqueue(line))retry.push_back(line);
            if(!retry.empty()) {
                std::lock_guard<std::mutex> lock(mutex);
                while(!retry.empty()) {
                    if(incoming.size()>=512){++refused;retry.pop_back();continue;}
                    incoming.push_front(std::move(retry.back()));retry.pop_back();
                }
            }
            if(!running)break; // Pending work is persisted for the next boot.
            auto now=std::chrono::steady_clock::now();
            if(now-lastSend<std::chrono::seconds(1))continue;
            lastSend=now;
            outbox->Step(time(nullptr),[this](const std::string& content){
                return CurlPost(url,MessagePayload(username,content));
            });
        }
    }
};
Sender& Get(Channel channel) {
    // Function-local initialization avoids preload constructor ordering races.
    static std::array<Sender,5> senders={Sender("chat"),Sender("deaths"),Sender("presence"),Sender("status"),Sender("admin")};
    return senders[(size_t)channel];
}
}
void Start(Channel c,const std::string& url,const std::string& name){Get(c).Start(url,name);}
void Stop(){for(auto c:{Channel::Chat,Channel::Deaths,Channel::Presence,Channel::Status,Channel::Admin})Get(c).Stop();}
bool IsEnabled(Channel c){return Get(c).Enabled();}
void Post(Channel c,const std::string& line){Get(c).Post(line);}
bool TryPost(Channel c,const std::string& line){return Get(c).Post(line);}
std::string StatusJson(){Json result=Json::object();for(auto c:{Channel::Chat,Channel::Deaths,Channel::Presence,Channel::Status,Channel::Admin})result[std::to_string((int)c)]=Get(c).Status();return result.dump();}
}
