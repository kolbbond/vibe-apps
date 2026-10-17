#include "LaunchCommands.hxx"

#include <memory>

#include <json/json.h>

namespace vibe::launcher {

namespace {

bool parseJson(std::string_view text, Json::Value& root) {
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
    std::string errors;
    return reader->parse(text.data(), text.data() + text.size(), &root, &errors);
}

bool plainWord(std::string_view s) {
    if(s.empty()) return false;
    for(const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '.' || c == '/' || c == '-' || c == '=' || c == ':' || c == '+' ||
                        c == ',' || c == '@' || c == '%';
        if(!ok) return false;
    }
    return true;
}

} // namespace

std::string shellQuote(std::string_view s) {
    if(plainWord(s)) return std::string{s};
    std::string out = "'";
    for(const char c : s) {
        if(c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

std::vector<std::string> hyprExecArgv(const std::string& binary, const std::vector<std::string>& args, const LaunchTarget& target) {
    std::string command;
    if(target.tv) {
        command = "[monitor " + target.output + "; fullscreen";
        if(std::string_view{kNoFocusRule}.size()) command += std::string{"; "} + kNoFocusRule;
        command += "] ";
    }
    command += shellQuote(binary);
    for(const std::string& arg : args) command += " " + shellQuote(arg);
    return {"hyprctl", "dispatch", "exec", command};
}

std::vector<std::string> closeArgv(const std::string& windowClass) {
    return {"hyprctl", "dispatch", "closewindow", "class:^(" + windowClass + ")$"};
}

std::vector<std::string> focusArgv(const std::string& address) {
    return {"hyprctl", "dispatch", "focuswindow", "address:" + address};
}

std::vector<std::string> buildArgv(const std::string& buildDir, const std::string& target) {
    return {"nice", "-n", "19", "cmake", "--build", buildDir, "--parallel", "4", "--target", target};
}

std::vector<MonitorInfo> parseMonitors(std::string_view json) {
    Json::Value root;
    if(!parseJson(json, root) || !root.isArray()) return {};
    std::vector<MonitorInfo> out;
    for(const Json::Value& m : root) {
        if(!m.isObject()) continue;
        out.push_back({m.get("id", -1).asInt(), m.get("name", "").asString(), m.get("focused", false).asBool()});
    }
    return out;
}

std::vector<ClientInfo> parseClients(std::string_view json, const std::vector<MonitorInfo>& monitors) {
    Json::Value root;
    if(!parseJson(json, root) || !root.isArray()) return {};
    std::vector<ClientInfo> out;
    for(const Json::Value& c : root) {
        if(!c.isObject()) continue;
        const int monitorId = c.get("monitor", -1).asInt();
        std::string monitor;
        for(const MonitorInfo& m : monitors)
            if(m.id == monitorId) monitor = m.name;
        out.push_back({c.get("class", "").asString(), monitor, c.get("address", "").asString()});
    }
    return out;
}

std::string parseActiveAddress(std::string_view json) {
    Json::Value root;
    if(!parseJson(json, root) || !root.isObject()) return {};
    return root.get("address", "").asString();
}

ActiveWindow parseActiveWindow(std::string_view json) {
    Json::Value root;
    if(!parseJson(json, root) || !root.isObject()) return {};
    return {root.get("address", "").asString(), root.get("monitor", -1).asInt()};
}

} // namespace vibe::launcher
