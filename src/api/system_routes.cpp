#include "api_routes.h"

#include "../runtime.h"
#include "../engine/chat.h"
#include "../engine/bans.h"
#include "../engine/process_event.h"

#include <cstdio>
#include <sstream>

namespace ApiRoutes {

void RegisterSystem(HttpServer& server) {
    server.Get("/api/hello", [](const HttpRequest&) -> HttpResponse {
        return JsonResponse(R"({"message":"Hello from RSDWRestAPI!","status":"ok"})");
    });

    server.Get("/api/health", [](const HttpRequest&) -> HttpResponse {
        bool ready = EngineReady();

        char uptime[32];
        snprintf(uptime, sizeof(uptime), "%.1f", Runtime::UptimeSeconds());

        std::ostringstream oss;
        oss << "{"
            << "\"status\":\"" << (Runtime::HasFailure() ? "degraded" : (ready ? "healthy" : "starting")) << "\","
            << "\"version\":\"" << Runtime::Version() << "\","
            << "\"uptimeSeconds\":" << uptime << ","
            << "\"engineReady\":" << (ready ? "true" : "false") << ","
            << "\"banStoreReady\":" << (DomBans::IsReady() ? "true" : "false") << ","
            << "\"chatHooked\":" << (DomChat::IsHooked() ? "true" : "false") << ","
            << "\"broadcastReady\":" << (DomChat::CanBroadcast() ? "true" : "false") << ","
            << "\"nativeCallsReady\":" << (GameThread::IsReady() && GameThread::FrameLayoutReady() ? "true" : "false")
            << "}";
        return JsonResponse(oss.str());
    });
}

}
