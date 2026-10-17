#include <Corrade/TestSuite/Tester.h>
#include <Corrade/Utility/DebugStl.h>

#include "LaunchCommands.hxx"

using namespace vibe::launcher;

namespace {

const char* kMonitors = R"([
 {"id": 0, "name": "DVI-D-1", "description": "Ancor Communications Inc VG248", "focused": true},
 {"id": 1, "name": "HDMI-A-3", "description": "ONN onn TV", "focused": false},
 {"id": 2, "name": "DP-1", "description": "Dell Inc. DELL S2721HS", "focused": false},
 {"id": 3, "name": "DP-3", "description": "Acer Technologies XZ320Q", "focused": false}
])";

const char* kClients = R"([
 {"address": "0x560fa921cf20", "pid": 3062, "class": "kitty", "title": "x", "monitor": 2, "fullscreen": 0},
 {"address": "0x560faa9b0000", "pid": 4242, "class": "alpha", "title": "alpha", "monitor": 1, "fullscreen": 2},
 {"address": "0x560faa9634d0", "pid": 1676873, "class": "bolt-launcher", "title": "x", "monitor": 9, "fullscreen": 0}
])";

using Argv = std::vector<std::string>;

} // namespace

struct LaunchCommandsTest : Corrade::TestSuite::Tester {
    explicit LaunchCommandsTest();

    void quotesOnlyWhenNeeded();
    void execHereHasNoRules();
    void execOnTvIsFullscreenOnOutputWithoutFocus();
    void closeBuildAndFocusArgv();
    void parsesMonitors();
    void parsesClientsWithMonitorNames();
    void malformedJsonGivesEmpty();
    void parsesActiveAddress();
};

LaunchCommandsTest::LaunchCommandsTest() {
    addTests({&LaunchCommandsTest::quotesOnlyWhenNeeded,
        &LaunchCommandsTest::execHereHasNoRules,
        &LaunchCommandsTest::execOnTvIsFullscreenOnOutputWithoutFocus,
        &LaunchCommandsTest::closeBuildAndFocusArgv,
        &LaunchCommandsTest::parsesMonitors,
        &LaunchCommandsTest::parsesClientsWithMonitorNames,
        &LaunchCommandsTest::malformedJsonGivesEmpty,
        &LaunchCommandsTest::parsesActiveAddress});
}

void LaunchCommandsTest::quotesOnlyWhenNeeded() {
    CORRADE_COMPARE(shellQuote("/opt/games/bin/alpha"), "/opt/games/bin/alpha");
    CORRADE_COMPARE(shellQuote("--tv=HDMI-A-3"), "--tv=HDMI-A-3");
    CORRADE_COMPARE(shellQuote("a b"), "'a b'");
    CORRADE_COMPARE(shellQuote("it's"), "'it'\\''s'");
    CORRADE_COMPARE(shellQuote("$HOME;rm"), "'$HOME;rm'");
    CORRADE_COMPARE(shellQuote(""), "''");
}

void LaunchCommandsTest::execHereHasNoRules() {
    const Argv argv = hyprExecArgv("/b/alpha", {"--mute", "a b"}, LaunchTarget{false, "HDMI-A-3"});
    CORRADE_COMPARE(argv, (Argv{"hyprctl", "dispatch", "exec", "/b/alpha --mute 'a b'"}));
}

void LaunchCommandsTest::execOnTvIsFullscreenOnOutputWithoutFocus() {
    const Argv argv = hyprExecArgv("/b/alpha", {}, LaunchTarget{true, "HDMI-A-3"});
    CORRADE_COMPARE(argv, (Argv{"hyprctl", "dispatch", "exec", "[monitor HDMI-A-3; fullscreen; noinitialfocus] /b/alpha"}));
}

void LaunchCommandsTest::closeBuildAndFocusArgv() {
    CORRADE_COMPARE(closeArgv("alpha"), (Argv{"hyprctl", "dispatch", "closewindow", "class:^(alpha)$"}));
    CORRADE_COMPARE(focusArgv("0x1234"), (Argv{"hyprctl", "dispatch", "focuswindow", "address:0x1234"}));
    CORRADE_COMPARE(buildArgv("/v/build", "alpha"),
        (Argv{"nice", "-n", "19", "cmake", "--build", "/v/build", "--parallel", "4", "--target", "alpha"}));
}

void LaunchCommandsTest::parsesMonitors() {
    const auto monitors = parseMonitors(kMonitors);
    CORRADE_COMPARE(monitors.size(), 4u);
    CORRADE_COMPARE(monitors[1].id, 1);
    CORRADE_COMPARE(monitors[1].name, "HDMI-A-3");
    CORRADE_VERIFY(monitors[0].focused);
    CORRADE_VERIFY(!monitors[1].focused);
}

void LaunchCommandsTest::parsesClientsWithMonitorNames() {
    const auto clients = parseClients(kClients, parseMonitors(kMonitors));
    CORRADE_COMPARE(clients.size(), 3u);
    CORRADE_COMPARE(clients[1].windowClass, "alpha");
    CORRADE_COMPARE(clients[1].monitor, "HDMI-A-3");
    CORRADE_COMPARE(clients[1].address, "0x560faa9b0000");
    CORRADE_COMPARE(clients[0].monitor, "DP-1");
    CORRADE_COMPARE(clients[2].monitor, ""); // unknown monitor id
}

void LaunchCommandsTest::malformedJsonGivesEmpty() {
    CORRADE_VERIFY(parseMonitors("not json").empty());
    CORRADE_VERIFY(parseClients("{\"a\": 1}", {}).empty()); // not an array
}

void LaunchCommandsTest::parsesActiveAddress() {
    CORRADE_COMPARE(parseActiveAddress(R"({"address": "0x560faa9a7c10", "class": "kitty"})"), "0x560faa9a7c10");
    CORRADE_COMPARE(parseActiveAddress("{}"), ""); // no focused window
    CORRADE_COMPARE(parseActiveAddress("garbage"), "");
}

CORRADE_TEST_MAIN(LaunchCommandsTest)
