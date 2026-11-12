#pragma once
#include "store.h"
namespace Thc {
void Start();
void Poll();
void Stop();
Json Data();
Json ServiceStatus();
Json RequestRecovery();
bool AuditRequest(const Json& details);
void QueueChat(const std::string& guid,const std::string& name,const std::string& body);
void Audit(const std::string& action,const std::string& target,const std::string& result,bool success);
}
