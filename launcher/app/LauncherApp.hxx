#pragma once

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <smg/GuiBase.hh>
#include <vibe/game/ActionMap.hxx>
#include <vibe/game/GameList.hxx>
#include <vibe/game/Hud.hxx>
#include <vibe/game/InputSystem.hxx>
#include <vibe/game/SdlInput.hxx>

#include "Guards.hxx"
#include "LaunchCommands.hxx"
#include "NavRepeat.hxx"
#include "Process.hxx"
#include "Watch.hxx"

namespace vibe::launcher {

struct LauncherOptions {
    std::string gamesPath; // default: <exe dir>/../games.json
    std::string tvOutput;  // default: $VIBE_TV, else HDMI-A-3
    bool couch = false;
};

LauncherOptions parseLauncherOptions(int argc, char** argv);

// desk mode: a table of games (mouse); couch mode: big tiles on the TV (gamepad)
class LauncherApp : public smg::GuiBase {
public:
    LauncherApp(const Arguments& arguments, LauncherOptions options);

private:
    enum class CouchState { Picking, Starting, Playing };

    struct Relaunch {
        std::string name;
        bool tv = false;
    };

    void drawEvent() override;
    void anyEvent(SDL_Event& event) override;

    void reloadGames();
    void pollHyprland();
    const game::GameEntry* find(const std::string& name) const;
    std::optional<std::string> runningOn(const std::string& name) const; // monitor name
    void launch(const game::GameEntry& entry, bool tv);
    void stop(const game::GameEntry& entry);
    void rebuild(const game::GameEntry& entry);
    void updateRebuild();
    void launchCouchOnTv();
    void checkUpdates();
    void restartGame(const game::GameEntry& entry);
    void restartSelf();
    void drawDesk();
    void updateCouch(double dt);
    void drawCouch();

    LauncherOptions _options;
    std::string _selfPath;
    FileWatch _gamesWatch;             // reload build/games.json when it changes
    FileWatch _selfWatch;              // our own binary rebuilt
    bool _selfUpdated = false;
    UpdateTracker _updates;            // running games whose binary was rebuilt since
    std::set<std::string> _outdated;
    bool _autoRestart = false;
    LaunchGuard _launchGuard;          // no double launches while a window is appearing
    double _clock = 0.0;
    std::vector<game::GameEntry> _games;
    std::string _gamesError;
    std::vector<MonitorInfo> _monitors;
    std::map<std::string, std::string> _running; // window class -> monitor name
    double _pollIn = 0.0;

    BackgroundJob _build;
    std::string _building;
    std::optional<Relaunch> _afterBuild;       // where to relaunch once the build succeeds
    std::optional<Relaunch> _pendingRelaunch;  // waiting for the old window to close
    std::string _refocusAddress;               // desk window to hand focus back to after a TV launch
    double _refocusIn = -1.0;

    // couch mode
    game::ActionMap _actions;
    game::InputSystem _input;
    game::SdlInput _sdl;
    game::Hud _hud;
    std::array<NavRepeat, 4> _navX{};
    std::array<NavRepeat, 4> _navY{};
    int _selected = 0;
    CouchState _couch = CouchState::Picking;
    std::string _couchGame;
    double _couchTimeout = 0.0;
    double _startHeld = 0.0;

    std::chrono::steady_clock::time_point _lastFrame;
    bool _firstFrame = true;
};

} // namespace vibe::launcher
