#include "rcon_commands.h"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <iomanip>
#include "../thc/service.h"
#include "../utils/command_args.h"

namespace Rcon {

namespace {

std::mutex& RegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<Command>& Registry() {
    static std::vector<Command> registry;
    return registry;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return value;
}

}

void Register(const Command& command) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().push_back(command);
}

const std::vector<Command>& All() {
    return Registry();
}

std::string Dispatch(const std::string& line) {
    std::vector<std::string> parts;std::string parseError;
    if(!CommandArgs::Parse(line,parts,parseError))return "Invalid arguments: "+parseError;
    if(parts.empty())return "";
    std::string name=parts.front();
    std::vector<std::string> args(parts.begin()+1,parts.end());

    std::string wanted = Lower(name);

    Command matched;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        for (const Command& command : Registry()) {
            if (Lower(command.name) != wanted) continue;
            matched = command;
            found = true;
            break;
        }
    }

    if (!found) return "Unknown command '" + name + "'. Try 'help'.";

    try {
        auto result=matched.handler(args);
        if(wanted=="kick" || wanted=="ban" || wanted=="unban" || wanted=="give" || wanted=="broadcast") {
            auto lower=Lower(result);
            bool success=lower.rfind("kicked ",0)==0 || lower.rfind("banned ",0)==0 || lower.rfind("unbanned ",0)==0 || lower.rfind("gave ",0)==0 || lower.rfind("broadcast rpc invoked",0)==0;
            Thc::Audit("rcon:"+wanted,wanted=="broadcast" || args.empty()?"":args[0],result,success);
        }
        return result;
    } catch (const std::exception& e) {
        return std::string("Command failed: ") + e.what();
    } catch (...) {
        return "Command failed.";
    }
}

void RegisterAll() {
    RegisterSystemCommands();
    RegisterPlayerCommands();
}

}
