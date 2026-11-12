// Exercise the real native pump and native-call marshalling on fake UFunctions.
#include "engine/process_event.cpp"
#include "engine/native_call.cpp"
#include <cassert>
#include <iostream>

APIConfig g_Config;
namespace {
alignas(uintptr_t) unsigned char pumpFunction[0xE0]{};
alignas(uintptr_t) unsigned char nativeFunction[0xE0]{};
int chain;
int pumpCalls = 0;
int nativeCalls = 0;
void FakeProcessEvent(void*, void*, void*) {}
void FakePump(void*, void*, void*) { ++pumpCalls; }
void FakeNative(void* context, void* rawFrame, void* result) {
    auto frame = (unsigned char*)rawFrame;
    assert(*(uintptr_t*)(frame + 0x10) == (uintptr_t)nativeFunction);
    assert(*(uintptr_t*)(frame + 0x18) == (uintptr_t)context);
    assert(*(uintptr_t*)(frame + 0x20) == 0);
    assert(*(uintptr_t*)(frame + 0x38) == (uintptr_t)&chain);
    assert(*(uintptr_t*)(frame + 0x28) != 0);
    *(bool*)result = true;
    ++nativeCalls;
}
}
namespace Mem {
void Init() {}
bool Readable(const void* p, size_t size) {
    uintptr_t a = (uintptr_t)p;
    return a >= 0x10000 && a + size >= a;
}
bool InImage(const void* p) {
    return p == (void*)&FakePump || p == (void*)&FakeNative || p == (void*)&FakeProcessEvent;
}
}
namespace DomEngine {
Engine* g_Engine = nullptr;
bool Engine::Initialize() { initialized = true; return true; }
uintptr_t Engine::FindFunction(const char*, const char* name) const {
    return std::string(name) == "ServerUpdateCamera" ? (uintptr_t)pumpFunction : 0;
}
uintptr_t Engine::FindObject(const char*, bool) const { return 0; }
uintptr_t Engine::GetObjectByIndex(int32_t) const { return 0; }
}
int main() {
    g_Config.enableLogging = false;
    DomEngine::Engine engine;
    DomEngine::g_Engine = &engine;
    engine.Initialize();
    *(uint32_t*)(pumpFunction + 0xB0) = 0x400;
    *(uintptr_t*)(pumpFunction + 0xD8) = (uintptr_t)&FakePump;
    *(uintptr_t*)(pumpFunction + 0x50) = (uintptr_t)&chain;
    assert(GameThread::PumpAgeSeconds()==-1);
    // Simulate ProcessEvent having already been located by symbol lookup.
    GameThread::g_Ready = true;
    GameThread::g_ProcessEvent = (uintptr_t)&FakeProcessEvent;
    GameThread::g_Original = &FakeProcessEvent;
    assert(GameThread::InstallExecPump());
    auto hooked = *(uintptr_t*)(pumpFunction + 0xD8);
    assert(hooked != (uintptr_t)&FakePump);
    assert(GameThread::InstallExecPump());
    assert(*(uintptr_t*)(pumpFunction + 0xD8) == hooked);
    alignas(uintptr_t) unsigned char context[0x100]{};
    alignas(uintptr_t) unsigned char frame[0x200]{};
    *(uintptr_t*)(frame + 0x10) = (uintptr_t)pumpFunction;
    *(uintptr_t*)(frame + 0x18) = (uintptr_t)context;
    *(uintptr_t*)(frame + 0x38) = (uintptr_t)&chain;
    ((void (*)(void*, void*, void*))hooked)(context, frame, nullptr);
    assert(pumpCalls == 1);
    assert(GameThread::PumpAgeSeconds()>=0 && GameThread::PumpAgeSeconds()<1);
    assert(GameThread::FrameLayoutReady());
    assert(GameThread::IsGameThread());
    *(uint32_t*)(nativeFunction + 0xB0) = 0x400;
    *(uintptr_t*)(nativeFunction + 0xD8) = (uintptr_t)&FakeNative;
    *(uintptr_t*)(nativeFunction + 0x50) = (uintptr_t)&chain;
    struct Params { int input = 1; bool output = false; } params;
    assert(GameThread::RunSync([&] {
        assert(NativeCall::Invoke((uintptr_t)context, (uintptr_t)nativeFunction,
                                  &params, offsetof(Params, output)));
    }));
    assert(params.output && nativeCalls == 1);
    GameThread::RemoveExecPump();
    assert(*(uintptr_t*)(pumpFunction + 0xD8) == (uintptr_t)&FakePump);
    std::cout << "PASS: symbol-path frame pump, pump idempotence, frame learning, native call layout, hook restoration\n";
}
