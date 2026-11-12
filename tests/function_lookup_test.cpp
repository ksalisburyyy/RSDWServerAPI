#include "engine/function_lookup.cpp"
#include <cassert>
#include <iostream>
#include <unordered_map>

APIConfig g_Config;
namespace {
struct Object { alignas(uintptr_t) unsigned char bytes[0x50]{}; } base, child, other, exact, derived, duplicate;
std::vector<uintptr_t> objects;
std::unordered_map<uintptr_t, std::string> names, classes;
uintptr_t Address(Object& o) { return (uintptr_t)o.bytes; }
void Outer(Object& f, Object& owner) { *(uintptr_t*)(f.bytes + DomEngine::Offsets::UObject_Outer) = Address(owner); }
void Super(Object& c, Object& parent) { *(uintptr_t*)(c.bytes + DomEngine::Offsets::UStruct_SuperStruct) = Address(parent); }
}
namespace Mem {
bool Readable(const void* p, size_t) { return p != nullptr; }
}
namespace DomEngine {
int32_t Engine::GetObjectCount() const { return (int32_t)objects.size(); }
uintptr_t Engine::GetObjectByIndex(int32_t index) const { return objects[index]; }
std::string Engine::GetObjectName(uintptr_t p) const { return names[p]; }
std::string Engine::GetObjectClassName(uintptr_t p) const { return classes[p]; }
}
int main() {
 g_Config.enableLogging = false;
 DomEngine::Engine engine;
 names[Address(base)]="PlayerChatComponent";
 names[Address(child)]="DerivedChatComponent";
 names[Address(other)]="Unrelated";
 for (auto* f : {&exact,&derived,&duplicate}) {
  names[Address(*f)]="Client_ReceiveChatMessage";
  classes[Address(*f)]="Function";
 }
 Outer(exact,base); Outer(derived,child); Outer(duplicate,other); Super(child,base);
 objects={Address(derived),Address(exact)};
 assert(engine.FindFunction("PlayerChatComponent","Client_ReceiveChatMessage")==Address(exact));
 objects={Address(derived)};
 assert(engine.FindFunction("PlayerChatComponent","Client_ReceiveChatMessage")==Address(derived));
 objects={Address(duplicate)};
 assert(engine.FindFunction("PlayerChatComponent","Client_ReceiveChatMessage")==0);
 Outer(duplicate,child); objects={Address(derived),Address(duplicate)};
 assert(engine.FindFunction("PlayerChatComponent","Client_ReceiveChatMessage")==0);
 assert(engine.FindFunction(nullptr,"Client_ReceiveChatMessage")==0);
 // A corrupt class cycle must terminate and refuse.
 Super(child,child); objects={Address(derived)};
 assert(engine.FindFunction("PlayerChatComponent","Client_ReceiveChatMessage")==0);
 std::cout << "PASS: exact precedence, derived class, unrelated refusal, ambiguity refusal, class-cycle bound\n";
}
