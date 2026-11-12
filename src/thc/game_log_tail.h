#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Thc {
// Bounded nonblocking regular-file reads; no subprocess, FIFO or symlink follows.
class GameLogTail {
    std::string path,partial,boundary;off_t offset=0;dev_t device=0;ino_t inode=0;
    bool initialized=false,discard=false;
public:
    bool available=false,complete=true;uint64_t generation=0;
    explicit GameLogTail(std::string filename):path(std::move(filename)){}
    std::vector<std::string> Read(){
        std::vector<std::string> lines;available=false;complete=false;
        int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return lines;
        struct stat st{};if(fstat(fd,&st) || !S_ISREG(st.st_mode)){close(fd);return lines;}
        available=true;
        if(!initialized){initialized=true;device=st.st_dev;inode=st.st_ino;offset=st.st_size;++generation;}
        bool changed=device!=st.st_dev || inode!=st.st_ino || st.st_size<offset;
        if(!changed && !boundary.empty()){
            std::string check(boundary.size(),'\0');auto n=pread(fd,check.data(),check.size(),offset-(off_t)check.size());
            changed=n!=(ssize_t)check.size() || check!=boundary;
        }
        if(changed){device=st.st_dev;inode=st.st_ino;offset=0;partial.clear();boundary.clear();discard=false;++generation;}
        char data[8192];size_t budget=256*1024;
        while(budget){auto n=pread(fd,data,std::min(sizeof(data),budget),offset);if(n<=0)break;offset+=n;budget-=(size_t)n;
            for(ssize_t i=0;i<n;++i){char c=data[i];if(c=='\n'){
                if(!discard){if(!partial.empty() && partial.back()=='\r')partial.pop_back();lines.push_back(std::move(partial));}
                partial.clear();discard=false;
            }else if(!discard){if(partial.size()<8192)partial+=c;else{partial.clear();discard=true;}}
            }
        }
        auto length=(size_t)std::min<off_t>(offset,64);boundary.assign(length,'\0');
        if(length && pread(fd,boundary.data(),length,offset-(off_t)length)!=(ssize_t)length)boundary.clear();
        complete=offset>=st.st_size && partial.empty() && !discard;
        close(fd);return lines;
    }
};
}
