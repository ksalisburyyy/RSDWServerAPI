#pragma once
#include <string>
namespace Thc {
inline std::string DiscordName(const std::string& name) {
    std::string escaped;
    for(char c:name) {
        if(c=='\\' || c=='*' || c=='_' || c=='`' || c=='~' || c=='|' || c=='[' || c==']')escaped+='\\';
        escaped+=c;
    }
    return escaped;
}
inline std::string NotificationStyle(const std::string& type,const std::string& name,const std::string& fallback) {
    if(type=="join")return "🟢 **"+DiscordName(name)+"** joined The High Celestial";
    if(type=="leave")return "🔴 **"+DiscordName(name)+"** left The High Celestial";
    if(type=="world_ready")return "🟢 **The High Celestial is ONLINE!**";
    if(type=="shutdown")return "🔴 **The High Celestial is OFFLINE!**";
    if(type=="world_unavailable")return "🔴 **THC monitoring is unavailable** — check the game and API.";
    if(type=="starting")return "⏳ **The High Celestial is starting…**";
    auto text=!name.empty() && fallback.rfind(name,0)==0?"**"+DiscordName(name)+"**"+fallback.substr(name.size()):fallback;
    if(type=="chat")return "💬 "+text;
    if(type=="kill")return "⚔️ "+text;
    if(type=="death")return "💀 "+text;
    if(type=="admin")return "🛡️ **Admin audit**\n"+text;
    if(type=="save")return "💾 "+text;
    if(type=="save_failed")return "🚨 "+text;
    return fallback;
}
inline bool StyledNotification(const std::string& type) {
    return type=="join" || type=="leave" || type=="world_ready" || type=="shutdown" || type=="world_unavailable" || type=="starting" || type=="chat" || type=="kill" || type=="death" || type=="admin" || type=="save" || type=="save_failed";
}
}
