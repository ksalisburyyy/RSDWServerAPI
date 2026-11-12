#include "delivery.h"
#include "../utils/atomic_file.h"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace Discord {
namespace {
Json Empty() { return {{"schema",1},{"next",uint64_t{1}},{"pending",Json::array()},{"failed",Json::array()},{"sent",uint64_t{0}}}; }
std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> result;
    for(size_t pos=0;pos<text.size();) {
        size_t end=std::min(pos+1900,text.size());
        if(end<text.size()) while(end>pos && ((unsigned char)text[end]&0xc0)==0x80) --end;
        if(end==pos) return {};
        result.push_back(text.substr(pos,end-pos));pos=end;
    }
    return result;
}
struct Temp {
    std::string path;
    int fd=-1;
    Temp() { char name[]="/tmp/thc-webhook-XXXXXX";fd=mkstemp(name);if(fd>=0) {path=name;fcntl(fd,F_SETFD,FD_CLOEXEC);} }
    ~Temp(){if(fd>=0)close(fd);if(!path.empty())unlink(path.c_str());}
};
}
DeliveryQueue::DeliveryQueue(std::string file):path(std::move(file)),state(Empty()){}
bool DeliveryQueue::Load() {
    std::lock_guard<std::mutex> lock(mutex);
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        if(std::filesystem::exists(path)) {
            if(std::filesystem::file_size(path)>8*1024*1024) throw std::runtime_error("oversize");
            std::ifstream file(path); Json loaded; file>>loaded;
            if(loaded.at("schema")!=1 || !loaded.at("pending").is_array() || loaded["pending"].size()>1000 ||
               !loaded.at("failed").is_array() || loaded["failed"].size()>100 ||
               !loaded.at("next").is_number_unsigned() || !loaded.at("sent").is_number_unsigned()) throw std::runtime_error("schema");
            for(auto& item:loaded["pending"]) {
                if(!item.at("id").is_number_unsigned() || !item.at("content").is_string() || item["content"].get_ref<const std::string&>().size()>1900 ||
                   !item.at("attempt").is_number_integer() || !item.at("due").is_number_integer()) throw std::runtime_error("item");
            }
            for(auto& item:loaded["pending"])if(item["attempt"].get<int64_t>()<0 || item["attempt"].get<int64_t>()>21 || item["due"].get<int64_t>()<0)throw std::runtime_error("invalid retry");
            state=std::move(loaded);
        }
        ready=true;error.clear();return true;
    } catch(...) { ready=false;error="outbox unreadable; original preserved";return false; }
}
bool DeliveryQueue::Persist(const Json& candidate) {
    std::string reason;
    try {
        if(!WriteFileAtomically(path,candidate.dump(),reason)){error="outbox save failed";return false;}
    }catch(...){error="outbox serialization failed; original preserved";return false;}
    return true;
}
bool DeliveryQueue::Enqueue(const std::string& text) {
    const auto chunks=Split(text);
    if(chunks.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if(!ready) return false;
    if(state["pending"].size()+chunks.size()>1000) {error="outbox full; new message refused";return false;}
    auto next=state;
    for(auto& chunk:chunks) {
        next["pending"].push_back({{"id",next["next"]},{"content",chunk},{"attempt",0},{"due",int64_t{0}}});
        next["next"]=next["next"].get<uint64_t>()+1;
    }
    if(!Persist(next)) return false;
    state=std::move(next);return true;
}
bool DeliveryQueue::Step(int64_t now,const std::function<Response(const std::string&)>& transport) {
    std::unique_lock<std::mutex> single(processing,std::try_to_lock);if(!single.owns_lock())return false;
    Json item;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(!ready || state["pending"].empty() || state["pending"][0]["due"].get<int64_t>()>now) return false;
        item=state["pending"][0];
    }
    Response response;
    try {response=transport(item["content"]);} catch(...) {}
    std::lock_guard<std::mutex> lock(mutex);
    auto next=state;lastHttp=response.status;
    if(response.status>=200 && response.status<300) {
        next["pending"].erase(next["pending"].begin()); next["sent"]=next["sent"].get<uint64_t>()+1;error.clear();
    } else if(response.status>=400 && response.status<500 && response.status!=408 && response.status!=429) {
        item["httpStatus"]=response.status;next["failed"].push_back(item);next["pending"].erase(next["pending"].begin());
        if(next["failed"].size()>100) next["failed"].erase(next["failed"].begin());
        error="webhook rejected a message; inspect failed outbox";
    } else {
        int attempt=std::min(item["attempt"].get<int>(),20)+1;
        next["pending"][0]["attempt"]=attempt;
        int delay=std::max(1,std::min(300,1<<std::min(attempt,8)));
        if(response.retryAfter>0) delay=std::max(delay,std::min(response.retryAfter,86400));
        next["pending"][0]["due"]=now+delay;error="delivery pending retry";
    }
    if(!Persist(next)) return false;
    state=std::move(next);return true;
}
Json DeliveryQueue::Status()const {std::lock_guard<std::mutex> lock(mutex);return {{"loaded",ready},{"pending",state["pending"].size()},{"failed",state["failed"].size()},{"sent",state["sent"]},{"lastHttpStatus",lastHttp},{"error",error}};}
bool ValidWebhook(const std::string& url) {
    for(const auto* prefix:{"https://discord.com/api/webhooks/","https://discordapp.com/api/webhooks/","https://canary.discord.com/api/webhooks/","https://ptb.discord.com/api/webhooks/"}) {
        if(url.rfind(prefix,0)!=0)continue;
        auto rest=url.substr(strlen(prefix));auto slash=rest.find('/');
        if(slash==std::string::npos || slash==0 || slash+1>=rest.size() || rest.substr(0,slash).find_first_not_of("0123456789")!=std::string::npos)return false;
        for(unsigned char c:url)if(c<=0x20 || c>=0x7f || c=='\\' || c=='\'' || c=='"' || c=='#')return false;
        return true;
    }
    return false;
}
bool ValidThreadId(const std::string& id){
    if(id.empty())return true;
    if(id.size()<16 || id.size()>20 || id.front()=='0' || id.find_first_not_of("0123456789")!=std::string::npos)return false;
    uint64_t value=0;auto parsed=std::from_chars(id.data(),id.data()+id.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==id.data()+id.size() && value>0;
}
std::string WebhookThread(const std::string& url){
    auto q=url.find('?');if(q==std::string::npos)return {};
    std::string found;
    for(size_t start=q+1;start<url.size();){auto end=url.find('&',start);auto item=url.substr(start,end==std::string::npos?end:end-start);if(item.rfind("thread_id=",0)==0){auto id=item.substr(10);if(!ValidThreadId(id) || id.empty() || !found.empty())return {};found=id;}if(end==std::string::npos)break;start=end+1;}
    return found;
}
std::string RouteWebhook(const std::string& url,const std::string& thread){
    if(url.empty())return {};
    if(!ValidWebhook(url) || !ValidThreadId(thread))return {};
    if(thread.empty())return url; // Existing working URLs are preserved verbatim.
    auto q=url.find('?');std::string result=url.substr(0,q);std::vector<std::string> query;
    if(q!=std::string::npos)for(size_t start=q+1;start<url.size();){auto end=url.find('&',start);auto item=url.substr(start,end==std::string::npos?end:end-start);if(!item.empty() && item.rfind("thread_id=",0)!=0)query.push_back(item);if(end==std::string::npos)break;start=end+1;}
    query.push_back("thread_id="+thread);for(size_t i=0;i<query.size();++i)result+=(i?"&":"?")+query[i];return result;
}
Response CurlPost(const std::string& url,const std::string& payload) {
    if(!ValidWebhook(url))return {400,0};
    Temp input,output,headers;
    if(input.fd<0 || output.fd<0 || headers.fd<0)return {};
    size_t offset=0;
    while(offset<payload.size()) {ssize_t n=write(input.fd,payload.data()+offset,payload.size()-offset);if(n<0 && errno==EINTR)continue;if(n<=0)return {};offset+=(size_t)n;}
    lseek(input.fd,0,SEEK_SET);
    // No shell. Strip preload variables so curl cannot recursively load this mod.
    std::vector<std::string> environment;
    for(char** p=environ;*p;++p) {std::string v=*p;if(v.rfind("LD_PRELOAD=",0)!=0 && v.rfind("LD_AUDIT=",0)!=0)environment.push_back(v);}
    std::vector<char*> env;for(auto& v:environment)env.push_back(v.data());env.push_back(nullptr);
    std::vector<std::string> args={"curl","--silent","--connect-timeout","3","--max-time","8","--proto","=https",
        "--request","POST","--header","Content-Type: application/json","--data-binary","@-","--output","/dev/null",
        "--dump-header",headers.path,"--write-out","%{http_code}","--",url};
    std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if(posix_spawn_file_actions_init(&actions)!=0)return {};
    posix_spawn_file_actions_adddup2(&actions,input.fd,STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions,output.fd,STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    pid_t child=0;int result=posix_spawnp(&child,"curl",&actions,nullptr,argv.data(),env.data());
    posix_spawn_file_actions_destroy(&actions);if(result!=0)return {};
    int status=0;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return {};}
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=0)return {};
    lseek(output.fd,0,SEEK_SET);char code[16]{};read(output.fd,code,sizeof(code)-1);
    Response response;response.status=atoi(code);
    std::ifstream file(headers.path);std::string line;
    while(std::getline(file,line)) {
        std::transform(line.begin(),line.end(),line.begin(),[](unsigned char c){return (char)tolower(c);});
        if(line.rfind("retry-after:",0)==0)try {response.retryAfter=std::clamp((int)std::ceil(std::stod(line.substr(12))),0,86400);}catch(...){}
    }
    return response;
}
}
