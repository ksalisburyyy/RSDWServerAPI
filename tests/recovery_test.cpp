#include "thc/recovery.h"
#include <cassert>
#include <iostream>
int main(){Thc::RecoveryQueue queue;assert(queue.Status()["state"]=="idle");
 auto request=queue.Request(100);assert(request["id"]==1 && request["state"]=="queued");assert(queue.Request(101)["id"]==1);
 assert(queue.Take() && !queue.Take());assert(queue.Status()["state"]=="checking" && queue.Request(102)["id"]==1);
 queue.Finish(103,true);assert(queue.Status()["state"]=="checked" && queue.Status()["completedAt"]==103);
 assert(queue.Request(104)["id"]==2);assert(queue.Take());queue.Finish(105,false);assert(queue.Status()["message"].get<std::string>().find("failed")!=std::string::npos);
 std::cout<<"PASS: asynchronous compatibility retries coalesce, execute once and distinguish attempted checks from readiness\n";}
