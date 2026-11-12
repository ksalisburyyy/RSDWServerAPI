#include "thc/event_page.h"
#include <cassert>
#include <iostream>
int main(){Thc::Json state={{"events",Thc::Json::array()},{"nextEvent",1101u}};
 for(uint64_t id=101;id<=1100;++id)state["events"].push_back({{"id",id}});
 auto page=Thc::EventPage(state,0,500,true);assert(page["events"].size()==500 && page["events"][0]["id"]==601 && page["events"].back()["id"]==1100 && page["hasMore"]==false);
 page=Thc::EventPage(state,0,500,false);assert(page["events"][0]["id"]==101 && page["events"].back()["id"]==600 && page["hasMore"]==true);
 page=Thc::EventPage(state,600,500,false);assert(page["events"][0]["id"]==601 && page["nextAfter"]==1100);
 page=Thc::EventPage(state,20,2,false);assert(page["truncated"]==true);
 page=Thc::EventPage(state,1100,500,false);assert(page["events"].empty());
 state={{"events",Thc::Json::array()},{"nextEvent",1u}};page=Thc::EventPage(state,0,500,true);assert(page["events"].empty() && page["latestId"]==0);
 std::cout<<"PASS: newest 500 retained events, unchanged incremental cursor ordering, pagination/truncation and empty history\n";
}
