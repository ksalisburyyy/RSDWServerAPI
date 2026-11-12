#pragma once
#include "store.h"
#include <mutex>
namespace Thc {
class RecoveryQueue {
 mutable std::mutex lock;
 uint64_t id=0;bool pending=false,running=false;
 int64_t requestedAt=0,completedAt=0;
 std::string result="No manual compatibility check requested";
 Json ReadLocked()const{return {{"id",id},{"state",pending?"queued":running?"checking":completedAt?"checked":"idle"},
  {"requestedAt",requestedAt},{"completedAt",completedAt},{"message",result}};}
public:
 Json Request(int64_t now){std::lock_guard<std::mutex> guard(lock);
  if(!pending && !running){++id;pending=true;requestedAt=now;completedAt=0;result="Validated discovery retry queued; readiness is checked separately";}
  return ReadLocked();
 }
 bool Take(){std::lock_guard<std::mutex> guard(lock);if(!pending)return false;pending=false;running=true;result="Retrying existing validated discovery paths";return true;}
 void Finish(int64_t now,bool success){std::lock_guard<std::mutex> guard(lock);running=false;completedAt=now;
  result=success?"Discovery attempt completed; inspect readiness below. This does not prove gameplay actions work":"Discovery attempt failed; existing validation safeguards remain active";
 }
 Json Status()const{std::lock_guard<std::mutex> guard(lock);return ReadLocked();}
};
inline RecoveryQueue& Recovery(){static RecoveryQueue queue;return queue;}
}
