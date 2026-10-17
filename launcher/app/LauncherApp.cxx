#include "LauncherApp.hxx"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include <Magnum/Math/Color.h>

using namespace Magnum;
using namespace Magnum::Math::Literals;

namespace vibe::launcher {

namespace {

std::string selfPath() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).string();
}

} // namespace

LauncherOptions parseLauncherOptions(int argc, char** argv) {
    LauncherOptions options;
    for(int i = 1; i < argc; ++i) {
        const bool hasValue = i + 1 < argc;
        if(std::strcmp(argv[i], "--couch") == 0)
            options.couch = true;
        else if(std::strcmp(argv[i], "--games") == 0 && hasValue)
            options.gamesPath = argv[++i];
        else if(std::strcmp(argv[i], "--tv") == 0 && hasValue)
            options.tvOutput = argv[++i];
    }
    if(options.gamesPath.empty())
        options.gamesPath = (std::filesystem::path{selfPath()}.parent_path() / ".." / "games.json").lexically_normal().string();
    if(options.tvOutput.empty()) {
        const char* tv = std::getenv("VIBE_TV");
        options.tvOutput = tv && *tv ? tv : "HDMI-A-3";
    }
    return options;
}

LauncherApp::LauncherApp(const Arguments& arguments, LauncherOptions options)
    : smg::GuiBase{arguments},
      _options{std::move(options)},
      _selfPath{selfPath()},
      _gamesWatch{_options.gamesPath},
      _selfWatch{_selfPath},
      _input{_actions, 4},
      _sdl{_actions} {
    setWindowTitle(_options.couch ? "vibe launcher (couch)" : "vibe launcher");
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    // couch navigation: both keyboard halves always, pads as they connect (slots 2-3)
    _input.assign(0, game::Source::keyboard(game::Source::KeyboardLeft));
    _input.assign(1, game::Source::keyboard(game::Source::KeyboardRight));
    reloadGames();
    pollHyprland();
}

void LauncherApp::anyEvent(SDL_Event& event) { _sdl.handleEvent(event, _input); }

void LauncherApp::reloadGames() {
    std::ifstream file{_options.gamesPath};
    if(!file.is_open()) {
        _games.clear();
        _gamesError = "cannot read " + _options.gamesPath + " (configure the build first)";
        return;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::vector<game::GameEntry> games;
    std::string error;
    if(!game::loadGameList(text.str(), games, error)) {
        _gamesError = error;
        return;
    }
    const std::string selectedName = _selected < static_cast<int>(_games.size()) ? _games[static_cast<std::size_t>(_selected)].name : "";
    _games = std::move(games);
    _gamesError.clear();
    const auto kept = std::find_if(_games.begin(), _games.end(), [&](const game::GameEntry& e) { return e.name == selectedName; });
    _selected = kept != _games.end() ? static_cast<int>(kept - _games.begin())
                                     : std::clamp(_selected, 0, std::max(0, static_cast<int>(_games.size()) - 1));
}

void LauncherApp::pollHyprland() {
    _monitors = parseMonitors(runProcess({"hyprctl", "monitors", "-j"}).output);
    _running.clear();
    for(const ClientInfo& client : parseClients(runProcess({"hyprctl", "clients", "-j"}).output, _monitors))
        _running[client.windowClass] = client.monitor;
    for(const auto& [windowClass, monitor] : _running) _launchGuard.seen(windowClass);

    if(_gamesWatch.changed()) {
        reloadGames();
        std::fprintf(stderr, "launcher: games.json changed, %zu games\n", _games.size());
    }
    if(_selfWatch.changed()) _selfUpdated = true;
    checkUpdates();

    if(_pendingRelaunch && !runningOn(_pendingRelaunch->name)) {
        if(const game::GameEntry* entry = find(_pendingRelaunch->name)) launch(*entry, _pendingRelaunch->tv);
        _pendingRelaunch.reset();
    }
}

const game::GameEntry* LauncherApp::find(const std::string& name) const {
    const auto it = std::find_if(_games.begin(), _games.end(), [&](const game::GameEntry& e) { return e.name == name; });
    return it == _games.end() ? nullptr : &*it;
}

std::optional<std::string> LauncherApp::runningOn(const std::string& name) const {
    const auto it = _running.find(name);
    if(it == _running.end()) return std::nullopt;
    return it->second;
}

void LauncherApp::launch(const game::GameEntry& entry, bool tv) {
    // one copy: not while it runs, nor while its window is still appearing (double click)
    if(!_launchGuard.tryLaunchIfStopped(entry.name, runningOn(entry.name).has_value(), _clock)) return;
    ActiveWindow previous;
    if(tv) // noinitialfocus alone does not keep focus on Hyprland 0.55 (verified in Task 2)
        previous = parseActiveWindow(runProcess({"hyprctl", "activewindow", "-j"}).output);
    runProcess(hyprExecArgv(entry.binary, entry.args, LaunchTarget{tv, _options.tvOutput}));
    _updates.started(entry.name, modifiedTime(entry.binary));
    if(tv && shouldHandBack(previous, _monitors, _options.tvOutput)) {
        // give focus back to the desk once the game has mapped (never to a TV window:
        // focusing under the fullscreen game would un-fullscreen it)
        _refocusAddress = previous.address;
        _refocusIn = 1.0;
    }
    _pollIn = 0.5; // refresh status soon
}

void LauncherApp::checkUpdates() {
    _outdated.clear();
    for(const game::GameEntry& entry : _games) {
        if(!runningOn(entry.name)) {
            _updates.stopped(entry.name);
            continue;
        }
        if(!_updates.updated(entry.name, modifiedTime(entry.binary))) continue;
        _outdated.insert(entry.name);
        // the launcher's own Rebuild relaunches by itself
        if(_autoRestart && _building != entry.name && !_pendingRelaunch) restartGame(entry);
    }
}

void LauncherApp::restartGame(const game::GameEntry& entry) {
    const std::optional<std::string> on = runningOn(entry.name);
    if(!on || _pendingRelaunch) return;
    stop(entry);
    _pendingRelaunch = Relaunch{entry.name, *on == _options.tvOutput}; // relaunched once the old window is gone
}

void LauncherApp::restartSelf() {
    std::vector<std::string> args{"--games", _options.gamesPath, "--tv", _options.tvOutput};
    if(_options.couch) args.emplace_back("--couch");
    runProcess(hyprExecArgv(_selfPath, args, LaunchTarget{_options.couch, _options.tvOutput}));
    exit();
}

void LauncherApp::stop(const game::GameEntry& entry) {
    runProcess(closeArgv(entry.name));
    _pollIn = 0.3;
}

void LauncherApp::rebuild(const game::GameEntry& entry) {
    const std::string buildDir = std::filesystem::path{_options.gamesPath}.parent_path().string();
    if(!_build.start(buildArgv(buildDir, entry.name))) return;
    _building = entry.name;
    const std::optional<std::string> on = runningOn(entry.name);
    if(on)
        _afterBuild = Relaunch{entry.name, *on == _options.tvOutput};
    else
        _afterBuild.reset();
}

void LauncherApp::updateRebuild() {
    const std::optional<int> status = _build.takeResult();
    if(!status) return;
    // only if it is still running: a game stopped during its rebuild stays stopped
    if(*status == 0 && _afterBuild && runningOn(_afterBuild->name)) {
        if(const game::GameEntry* entry = find(_afterBuild->name)) stop(*entry);
        _pendingRelaunch = _afterBuild; // launched by pollHyprland once the old window is gone
    }
    _afterBuild.reset();
    _building.clear();
}

void LauncherApp::launchCouchOnTv() {
    const ActiveWindow previous = parseActiveWindow(runProcess({"hyprctl", "activewindow", "-j"}).output);
    runProcess(hyprExecArgv(_selfPath, {"--couch", "--games", _options.gamesPath, "--tv", _options.tvOutput},
        LaunchTarget{true, _options.tvOutput}));
    if(shouldHandBack(previous, _monitors, _options.tvOutput)) {
        _refocusAddress = previous.address;
        _refocusIn = 1.0;
    }
}

void LauncherApp::drawDesk() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("vibe launcher", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    if(ImGui::Button("Reload games")) reloadGames();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if(ImGui::BeginCombo("TV output", _options.tvOutput.c_str())) {
        for(const MonitorInfo& m : _monitors)
            if(ImGui::Selectable(m.name.c_str(), m.name == _options.tvOutput)) _options.tvOutput = m.name;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if(ImGui::Button("Couch mode on TV")) launchCouchOnTv();
    ImGui::SameLine();
    ImGui::Checkbox("auto-restart on rebuild", &_autoRestart);
    if(_selfUpdated) {
        ImGui::SameLine();
        if(ImGui::Button("Launcher updated: restart")) restartSelf();
    }
    if(!_gamesError.empty()) ImGui::TextColored({1.0f, 0.45f, 0.45f, 1.0f}, "%s", _gamesError.c_str());

    if(ImGui::BeginTable("games", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Game");
        ImGui::TableSetupColumn("Players");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Actions");
        ImGui::TableHeadersRow();
        for(const game::GameEntry& entry : _games) {
            ImGui::PushID(entry.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.title.c_str());
            if(!entry.description.empty()) ImGui::TextDisabled("%s", entry.description.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.players.c_str());

            ImGui::TableNextColumn();
            const std::optional<std::string> on = runningOn(entry.name);
            const bool starting = !on && _launchGuard.pending(entry.name, _clock);
            if(starting)
                ImGui::TextUnformatted("starting...");
            else if(!on)
                ImGui::TextUnformatted("stopped");
            else if(*on == _options.tvOutput)
                ImGui::TextColored({0.45f, 1.0f, 0.55f, 1.0f}, "running on TV");
            else
                ImGui::Text("running here (%s)", on->c_str());
            if(_outdated.count(entry.name)) {
                ImGui::SameLine();
                ImGui::TextColored({1.0f, 0.8f, 0.3f, 1.0f}, "updated");
                ImGui::SameLine();
                if(ImGui::SmallButton("Restart")) restartGame(entry);
            }

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(on.has_value() || starting);
            if(ImGui::Button("Play here")) launch(entry, false);
            ImGui::SameLine();
            if(ImGui::Button("Play on TV")) launch(entry, true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!on.has_value());
            if(ImGui::Button("Stop")) stop(entry);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(_build.running());
            if(ImGui::Button(_building == entry.name ? "Building..." : "Rebuild")) rebuild(entry);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("build log");
    ImGui::BeginChild("log", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
    for(const std::string& line : _build.log()) ImGui::TextUnformatted(line.c_str());
    if(_build.running()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void LauncherApp::updateCouch(double dt) {
    _sdl.poll(_input);
    _input.beginTick();
    for(const game::DeviceEvent& event : _input.deviceEvents()) {
        const game::Source pad = game::Source::gamepad(event.gamepadId);
        if(event.kind == game::DeviceEvent::Kind::Added) {
            if(_input.slotOf(pad) >= 0) continue;
            for(int slot = 2; slot < _input.maxPlayers(); ++slot)
                if(!_input.assigned(slot)) {
                    _input.assign(slot, pad);
                    break;
                }
        } else if(const int slot = _input.slotOf(pad); slot >= 0) {
            _input.release(slot);
        }
    }

    // any game on the TV owns the pads until its window is gone (they reach it in the
    // background too), including one started from the desk or relaunched by a rebuild
    std::vector<std::string> names;
    for(const game::GameEntry& entry : _games) names.push_back(entry.name);
    const bool gameUp = gameOnOutput(_running, names, _options.tvOutput);
    switch(_couch) {
    case CouchState::Starting:
        _couchTimeout -= dt;
        if(gameUp)
            _couch = CouchState::Playing;
        else if(_couchTimeout <= 0.0)
            _couch = CouchState::Picking; // it never appeared
        return;
    case CouchState::Playing:
        if(!gameUp) _couch = CouchState::Picking;
        return;
    case CouchState::Picking:
        if(gameUp) {
            for(const std::string& name : names)
                if(runningOn(name) == _options.tvOutput) _couchGame = name;
            _couch = CouchState::Playing;
            return;
        }
        // nobody is playing: pick up a rebuilt launcher right away
        if(_selfUpdated) {
            restartSelf();
            return;
        }
        break;
    }
    if(_games.empty()) return;

    const int columns = std::min(3, static_cast<int>(_games.size()));
    int step = 0;
    bool confirm = false;
    bool startDown = false;
    for(int slot = 0; slot < _input.maxPlayers(); ++slot) {
        if(!_input.assigned(slot)) continue;
        step += _navX[static_cast<std::size_t>(slot)].update(_input.axis(slot, game::ActionMap::NavX), dt);
        step += columns * _navY[static_cast<std::size_t>(slot)].update(_input.axis(slot, game::ActionMap::NavY), dt);
        confirm = confirm || _input.pressed(slot, game::ActionMap::Confirm);
        startDown = startDown || _input.down(slot, game::ActionMap::Start);
    }
    // pads beyond the assigned slots can still pick
    for(const auto& press : _input.unclaimedPresses())
        confirm = confirm || press.button.index == game::ActionMap::Confirm.index;

    _selected = std::clamp(_selected + step, 0, static_cast<int>(_games.size()) - 1);
    _startHeld = startDown ? _startHeld + dt : 0.0;
    if(_startHeld >= 2.0) {
        exit();
        return;
    }
    if(confirm) {
        const game::GameEntry& entry = _games[static_cast<std::size_t>(_selected)];
        launch(entry, true);
        _couchGame = entry.name;
        _couch = CouchState::Starting;
        _couchTimeout = 10.0;
    }
}

void LauncherApp::drawCouch() {
    const Vector2i fb = framebufferSize();
    const game::View full{Range2Di{{}, fb}, -1, fb.y() > 0 ? float(fb.x()) / float(fb.y()) : 1.0f};
    const Vector2 size = _hud.viewSize(full);
    const Color4 white = 0xffffffff_rgbaf;
    const Color4 grey = 0x9aa0aaff_rgbaf;

    if(_couch != CouchState::Picking) {
        const game::GameEntry* entry = find(_couchGame);
        const std::string title = entry ? entry->title : _couchGame;
        _hud.textCentered(full, size * 0.5f, white, (_couch == CouchState::Starting ? "starting " : "playing ") + title + "...", 2.5f);
        return;
    }

    _hud.textCentered(full, {size.x() * 0.5f, size.y() * 0.12f}, white, "vibe games", 3.0f);
    if(_games.empty()) {
        _hud.textCentered(full, size * 0.5f, grey, _gamesError.empty() ? "no games built" : _gamesError);
        return;
    }

    const int columns = std::min(3, static_cast<int>(_games.size()));
    const int rows = (static_cast<int>(_games.size()) + columns - 1) / columns;
    const float gap = 32.0f;
    const Vector2 tile{(size.x() - gap * float(columns + 1)) / float(columns),
        std::min(260.0f, (size.y() * 0.6f - gap * float(rows - 1)) / float(rows))};
    for(int i = 0; i < static_cast<int>(_games.size()); ++i) {
        const game::GameEntry& entry = _games[static_cast<std::size_t>(i)];
        const Vector2 min{gap + float(i % columns) * (tile.x() + gap), size.y() * 0.25f + float(i / columns) * (tile.y() + gap)};
        if(i == _selected) _hud.panel(full, min - Vector2{6.0f}, tile + Vector2{12.0f}, 0x00e5ffff_rgbaf);
        _hud.panel(full, min, tile, 0x1a1d24ff_rgbaf);
        const Vector2 center = min + tile * 0.5f;
        _hud.textCentered(full, center - Vector2{0.0f, 30.0f}, white, entry.title, 2.0f);
        if(!entry.players.empty()) _hud.textCentered(full, center + Vector2{0.0f, 10.0f}, grey, entry.players + " players");
        if(runningOn(entry.name)) _hud.textCentered(full, center + Vector2{0.0f, 40.0f}, 0x9dff00ff_rgbaf, "running");
    }
    _hud.textCentered(full, {size.x() * 0.5f, size.y() * 0.92f}, grey,
        "move: stick / d-pad / arrows     play: A / Enter / Space     hold Start / Esc 2 s: exit");
}

void LauncherApp::drawEvent() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = _firstFrame ? 0.0 : std::chrono::duration<double>(now - _lastFrame).count();
    _lastFrame = now;
    _firstFrame = false;
    _clock += dt;

    _pollIn -= dt;
    if(_pollIn <= 0.0) {
        pollHyprland();
        _pollIn = 1.0;
    }
    if(_refocusIn >= 0.0) {
        _refocusIn -= dt;
        if(_refocusIn < 0.0) runProcess(focusArgv(_refocusAddress));
    }
    updateRebuild();

    drawBegin();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    _hud.setFrame(framebufferSize(), Vector2{display.x, display.y});
    if(_options.couch) {
        updateCouch(dt);
        drawCouch();
    } else {
        drawDesk();
    }
    drawEnd();
}

} // namespace vibe::launcher
