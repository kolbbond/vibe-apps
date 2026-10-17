#pragma once

#include <map>
#include <string>
#include <vector>

#include "LaunchCommands.hxx"

namespace vibe::launcher {

// hand focus back after a TV launch only to a window that is not on the TV itself: focusing
// a window under the fullscreen game (e.g. the couch launcher) would un-fullscreen the game
inline bool shouldHandBack(const ActiveWindow& previous, const std::vector<MonitorInfo>& monitors, const std::string& tvOutput) {
    if(previous.address.empty()) return false;
    for(const MonitorInfo& m : monitors)
        if(m.id == previous.monitorId) return m.name != tvOutput;
    return true;
}

// is any known game running on `output`? (running: window class -> monitor name)
inline bool gameOnOutput(const std::map<std::string, std::string>& running, const std::vector<std::string>& games, const std::string& output) {
    for(const std::string& name : games) {
        const auto it = running.find(name);
        if(it != running.end() && it->second == output) return true;
    }
    return false;
}

// one launch per game until its window shows up (or kTimeout passes): no double launches
class LaunchGuard {
public:
    static constexpr double kTimeout = 10.0;

    bool pending(const std::string& name, double now) const {
        const auto it = _launched.find(name);
        return it != _launched.end() && now - it->second < kTimeout;
    }
    bool tryLaunch(const std::string& name, double now) {
        if(pending(name, now)) return false;
        _launched[name] = now;
        return true;
    }
    bool tryLaunchIfStopped(const std::string& name, bool running, double now) { return !running && tryLaunch(name, now); }
    void seen(const std::string& name) { _launched.erase(name); }

private:
    std::map<std::string, double> _launched; // name -> launch time
};

} // namespace vibe::launcher
