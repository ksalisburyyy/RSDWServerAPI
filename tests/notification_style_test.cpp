#include "thc/notification_style.h"
#include "discord/message.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace Thc;
    for(auto sample:{std::pair<const char*,const char*>{"join","🟢"},{"leave","🔴"},{"world_ready","🟢"},{"shutdown","🔴"}}){
        auto text=NotificationStyle(sample.first,"Callmebury","original");
        assert(text.rfind(sample.second,0)==0 && text.find("[THC #")==std::string::npos && StyledNotification(sample.first));
    }
    auto join=NotificationStyle("join","Name_*[x]","fallback");
    assert(join.find("**Name\\_\\*\\[x\\]**")!=std::string::npos && join.find("joined The High Celestial")!=std::string::npos);
    assert(NotificationStyle("chat","Name","Name: original chat") == "💬 **Name**: original chat" && StyledNotification("chat"));
    assert(NotificationStyle("death","Name","Name died (cause: unattributed)") == "💀 **Name** died (cause: unattributed)");
    assert(NotificationStyle("admin","","original audit") == "🛡️ **Admin audit**\noriginal audit");
    assert(NotificationStyle("unknown","Name","original evidence")=="original evidence");
    assert(NotificationStyle("world_unavailable","","fallback").find("OFFLINE")==std::string::npos);
    auto inherited=nlohmann::json::parse(Discord::MessagePayload("", "🟢 hello"));
    assert(!inherited.contains("username") && !inherited.contains("avatar_url"));
    assert(inherited["allowed_mentions"]["parse"].empty());
    auto explicitName=nlohmann::json::parse(Discord::MessagePayload("Custom name","body"));
    assert(explicitName["username"]=="Custom name" && explicitName["content"]=="body");
    std::cout<<"PASS: green joins/online, red leaves/offline, no diagnostic prefix on status/presence, Markdown-safe names category icons, evidence text preserved and unknown messages unchanged\n";
}
