#include "metrics.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace Thc {
nlohmann::json SystemMetrics(const std::string& directory) {
    // Read-only process/host metrics. Never run commands or inspect secrets.
    static std::mutex mutex;static double lastCpu=0,lastTime=0;
    std::lock_guard<std::mutex> lock(mutex);
    nlohmann::json result={{"cpuPercent",nullptr},{"rssBytes",nullptr},{"memoryTotalBytes",nullptr},
        {"diskUsedBytes",nullptr},{"diskTotalBytes",nullptr},{"processUptimeSeconds",nullptr},
        {"cpuBasis","process CPU divided by online CPUs"}};
    auto now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto cpus=std::max(1L,sysconf(_SC_NPROCESSORS_ONLN));
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0){
        double cpu=usage.ru_utime.tv_sec+usage.ru_utime.tv_usec/1e6+usage.ru_stime.tv_sec+usage.ru_stime.tv_usec/1e6;
        if(lastTime>0 && now>lastTime)result["cpuPercent"]=std::clamp((cpu-lastCpu)/(now-lastTime)/cpus*100,0.0,100.0);
        lastCpu=cpu;lastTime=now;
    }
    uint64_t pages=0,resident=0;std::ifstream statm("/proc/self/statm");
    if(statm>>pages>>resident)result["rssBytes"]=resident*(uint64_t)std::max(1L,sysconf(_SC_PAGESIZE));
    struct sysinfo info{};
    if(sysinfo(&info)==0){
        result["memoryTotalBytes"]=(uint64_t)info.totalram*info.mem_unit;
        std::ifstream process("/proc/self/stat");std::string line;
        if(std::getline(process,line)){
            auto end=line.rfind(')');
            if(end!=std::string::npos){std::istringstream fields(line.substr(end+1));std::string token;
                for(int field=3;field<=22 && fields>>token;++field)if(field==22){
                    try{double start=std::stoull(token)/(double)std::max(1L,sysconf(_SC_CLK_TCK));result["processUptimeSeconds"]=std::max(0.0,info.uptime-start);}catch(...){}
                }
            }
        }
    }
    struct statvfs disk{};
    if(statvfs(directory.c_str(),&disk)==0){
        result["diskTotalBytes"]=(uint64_t)disk.f_blocks*disk.f_frsize;
        result["diskUsedBytes"]=(uint64_t)(disk.f_blocks-disk.f_bavail)*disk.f_frsize;
    }
    return result;
}
}
