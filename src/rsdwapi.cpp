#include "api/api_routes.h"
#include "config/config.h"
#include "discord/webhook.h"
#include "engine/bans.h"
#include "engine/chat.h"
#include "engine/dom_engine.h"
#include "engine/native_call.h"
#include "engine/process_event.h"
#include "http/http_server.h"
#include "rcon/rcon_commands.h"
#include "rcon/rcon_server.h"
#include "runtime.h"
#include "thc/service.h"
#include "utils/logger.h"
#include "utils/memory.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

APIConfig g_Config;

namespace {

HttpServer* g_HttpServer = nullptr;
Rcon::Server* g_RconServer = nullptr;
std::thread* g_InitThread = nullptr;
std::atomic<bool> g_Shutdown{false};
std::atomic<bool> g_StoppedCleanly{false};

void ShutdownListeners() {
    if (g_StoppedCleanly.exchange(true)) return;
    g_Shutdown = true;

    GameThread::StopJobs();
    if (g_HttpServer) g_HttpServer->Stop();
    if (g_RconServer) g_RconServer->Stop();
    if (g_InitThread && g_InitThread->joinable()) g_InitThread->join();
    Thc::Stop();
    Discord::Stop();

    Runtime::BeginShutdown();
}

void StartListeners() {
    if (g_Config.rest.enabled) {
        g_HttpServer = new HttpServer();
        g_HttpServer->SetIpWhitelist(g_Config.rest.ipWhitelist);
        ApiRoutes::RegisterAll(*g_HttpServer);
        if (g_Config.rest.bearerToken.empty()) {
            LogMessage("RSDWRestAPI: REST bearer token is empty - the API is UNAUTHENTICATED. "
                       "Bind to 127.0.0.1 or set IPWhitelist unless that is intended.");
        }
        bool running = g_HttpServer->Start(g_Config.rest.port, g_Config.rest.bindAddress);
        Runtime::SetRestState(true, running, g_Config.rest.port);
        if (!running) LogMessage("RSDWRestAPI: REST listener failed to start");
    } else {
        Runtime::SetRestState(false, false, g_Config.rest.port);
        LogMessage("RSDWRestAPI: REST API disabled by settings.ini");
    }

    if (g_Config.rcon.enabled) {
        Rcon::RegisterAll();
        g_RconServer = new Rcon::Server();
        bool running = g_RconServer->Start(g_Config.rcon);
        Runtime::SetRconState(true, running, g_Config.rcon.port);
        if (!running) LogMessage("RSDWRestAPI: RCON listener failed to start");
    } else {
        Runtime::SetRconState(false, false, g_Config.rcon.port);
        LogMessage("RSDWRestAPI: RCON disabled by settings.ini");
    }
}

void InitThreadBody() {
    Mem::Init();

    APIConfig::EnsureConfigDirectory();
    std::string configPath = APIConfig::GetConfigPath();
    if (!g_Config.Load(configPath)) g_Config.GenerateSecrets();
    bool configSaved=g_Config.Save(configPath);

    GetActiveLogPath() = InitLogFile();
    Runtime::MarkStart();
    if(!configSaved)LogMessage("THC: configuration could not be saved; original file preserved");

    LogMessage("RSDWRestAPI " + std::string(Runtime::Version()) + " loaded");
    LogMessage("RSDWRestAPI: data directory " + GetBaseDir());

    DomBans::Load();
    Thc::Start();
    StartListeners();
    atexit(&ShutdownListeners);

    DomEngine::g_Engine = new DomEngine::Engine();

    for (int attempt = 1; !g_Shutdown && !DomEngine::g_Engine->IsInitialized(); attempt++) {
        if (DomEngine::g_Engine->Initialize()) break;

        int waitSeconds = attempt < 10 ? 1 : (attempt < 30 ? 5 : 30);

        if (attempt == 1) {
            LogMessage("RSDWRestAPI: waiting for the engine to finish loading");
        }

        for (int i = 0; i < waitSeconds * 10 && !g_Shutdown; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (!g_Shutdown && DomEngine::g_Engine->IsInitialized()) {
        LogMessage("RSDWRestAPI: engine ready, endpoints are live");

        NativeCall::LogImageSymbolReport();

        DomEngine::DiscoveryInfo discovery = DomEngine::g_Engine->GetDiscoveryInfo();
        bool eventReady = GameThread::Initialize(discovery.moduleBase, discovery.moduleEnd);
        bool pumpReady = GameThread::InstallExecPump();
        if (!eventReady && !pumpReady) {
            LogMessage("RSDWRestAPI: kick disabled - " + GameThread::Status());
        }

        DomChat::Initialize();

        while (!g_Shutdown) {
            for (int i = 0; i < 20 && !g_Shutdown; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (g_Shutdown) break;

            DomBans::Enforce();
            DomChat::Poll();

            Thc::Poll();
        }
    }
}

void InitThread(){
    try{InitThreadBody();}
    catch(...){Runtime::MarkFailure();fprintf(stderr,"[THC] initialization stopped after an internal error; game process left running\n");}
}

}

extern "C" __attribute__((constructor)) void RSDWApiLoad() {
    g_InitThread = new std::thread(InitThread);
}

extern "C" __attribute__((destructor)) void RSDWApiUnload() {
    g_Shutdown = true;
    Runtime::BeginShutdown();
}
