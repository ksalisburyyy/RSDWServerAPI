#pragma once
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
namespace PointerPatch {
// Swap one aligned data pointer, preserving the mapping's original permissions.
// No instruction rewriting, trampoline, extra page or executable allocation.
inline bool Replace(uintptr_t slot,uintptr_t expected,uintptr_t replacement){
    if(!slot || slot%alignof(uintptr_t) || !expected || !replacement)return false;
    FILE* maps=fopen("/proc/self/maps","r");if(!maps)return false;
    char line[1024],perms[5]{};unsigned long long begin=0,end=0;bool found=false;
    while(fgets(line,sizeof(line),maps))if(sscanf(line,"%llx-%llx %4s",&begin,&end,perms)==3 && slot>=begin && slot<=end-sizeof(uintptr_t)){found=true;break;}
    fclose(maps);if(!found || perms[0]!='r')return false;
    int protection=PROT_READ;if(perms[1]=='w')protection|=PROT_WRITE;if(perms[2]=='x')protection|=PROT_EXEC;
    long pageSize=sysconf(_SC_PAGESIZE);if(pageSize<=0)return false;
    uintptr_t page=slot&~((uintptr_t)pageSize-1);
    if(page<begin || page+(uintptr_t)pageSize>end)return false;
    bool writable=protection&PROT_WRITE;
    if(!writable && mprotect((void*)page,(size_t)pageSize,protection|PROT_WRITE))return false;
    bool changed=__atomic_compare_exchange_n((uintptr_t*)slot,&expected,replacement,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    if(!writable && mprotect((void*)page,(size_t)pageSize,protection)){
        // Retain a valid dispatcher chain if permissions cannot be restored.
        // The swap result still accurately reports whether the hook is installed.
    }
    return changed;
}
}
