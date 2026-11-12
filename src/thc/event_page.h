#pragma once
#include "store.h"
#include <algorithm>
namespace Thc {
inline Json EventPage(const Json& state,uint64_t after,size_t limit,bool latest){
 const auto& retained=state.at("events");Json events=Json::array();
 size_t start=latest && retained.size()>limit?retained.size()-limit:0;
 for(size_t i=start;i<retained.size() && events.size()<limit;++i)
  if(retained[i].at("id").get<uint64_t>()>after)events.push_back(retained[i]);
 auto newest=state.at("nextEvent").get<uint64_t>()-1;
 auto oldest=retained.empty()?uint64_t{0}:retained[0].at("id").get<uint64_t>();
 auto next=events.empty()?after:events.back().at("id").get<uint64_t>();
 return {{"events",events},{"latestId",newest},{"oldestId",oldest},{"nextAfter",next},
  {"hasMore",!events.empty() && next<newest},{"truncated",after>0 && oldest>after && oldest-after>1}};
}
}
