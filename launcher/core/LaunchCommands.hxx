#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vibe::launcher {

// Hyprland 0.55 exec rule: "noinitialfocus" keeps fullscreen working but does not stop a
// fullscreen launch from taking focus ("no_initial_focus" breaks the rule list), so
// LauncherApp also hands focus back after every TV launch (verified live in Task 2).
inline constexpr const char* kNoFocusRule = "noinitialfocus";

struct LaunchTarget {
    bool tv = false;
    std::string output; // monitor name used when tv
};

struct MonitorInfo {
    int id = -1;
    std::string name;
    bool focused = false;
};

struct ClientInfo {
    std::string windowClass;
    std::string monitor; // monitor name, empty if unknown
    std::string address;
};

// POSIX shell single-quoting; plain words pass through unchanged
std::string shellQuote(std::string_view s);

// hyprctl argv (no shell involved); the exec payload is run by Hyprland through sh -c
std::vector<std::string> hyprExecArgv(const std::string& binary, const std::vector<std::string>& args, const LaunchTarget& target);
std::vector<std::string> closeArgv(const std::string& windowClass);
std::vector<std::string> focusArgv(const std::string& address);
std::vector<std::string> buildArgv(const std::string& buildDir, const std::string& target);

// hyprctl -j parsing; malformed input gives empty results
std::vector<MonitorInfo> parseMonitors(std::string_view json);
std::vector<ClientInfo> parseClients(std::string_view json, const std::vector<MonitorInfo>& monitors);
std::string parseActiveAddress(std::string_view json);

struct ActiveWindow {
    std::string address; // empty when nothing is focused
    int monitorId = -1;
};
ActiveWindow parseActiveWindow(std::string_view json);

} // namespace vibe::launcher
