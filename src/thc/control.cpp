#include "control.h"
#include "../config/config.h"
#include <sys/socket.h>
#include <cstring>
#include <sys/un.h>
#include <unistd.h>
namespace Thc {
nlohmann::json ControlRequest(const nlohmann::json& request){
 auto path=GetBaseDir()+"/thc/control.sock";
 if(path.size()>=sizeof(sockaddr_un::sun_path))return {{"ok",false},{"error","control socket path too long"}};
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return {{"ok",false},{"error","control socket unavailable"}};
 timeval timeout{15,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 sockaddr_un address{};address.sun_family=AF_UNIX;strncpy(address.sun_path,path.c_str(),sizeof(address.sun_path)-1);
 if(connect(fd,(sockaddr*)&address,sizeof(address))!=0){close(fd);return {{"ok",false},{"error","local control service is not configured or running"}};}
 auto data=request.dump()+"\n";size_t sent=0;
 while(sent<data.size()){auto n=send(fd,data.data()+sent,data.size()-sent,MSG_NOSIGNAL);if(n<=0){close(fd);return {{"ok",false},{"error","control request failed"}};}sent+=(size_t)n;}
 std::string reply;char buffer[4096];
 while(reply.find('\n')==std::string::npos && reply.size()<512*1024){auto n=recv(fd,buffer,sizeof(buffer),0);if(n<=0)break;reply.append(buffer,(size_t)n);}
 close(fd);auto end=reply.find('\n');auto parsed=nlohmann::json::parse(reply.substr(0,end),nullptr,false);
 if(!parsed.is_object())return {{"ok",false},{"error","control service timed out or returned an invalid reply"}};
 return parsed;
}
}
