#include <Corrade/TestSuite/Tester.h>
#include <Corrade/Utility/DebugStl.h>

#include "Guards.hxx"
#include "LaunchCommands.hxx"

using namespace vibe::launcher;

namespace {
const std::vector<MonitorInfo> kMonitors{{0, "DVI-D-1", true}, {1, "HDMI-A-3", false}, {2, "DP-1", false}};
}

struct GuardsTest : Corrade::TestSuite::Tester {
    explicit GuardsTest();

    void parsesActiveWindowMonitor();
    void handsFocusBackOnlyToDeskWindows();
    void anyKnownGameOnTheTvCounts();
    void launchGuardBlocksUntilSeenOrTimeout();
    void runningGameIsNotRelaunched();
};

GuardsTest::GuardsTest() {
    addTests({&GuardsTest::parsesActiveWindowMonitor,
        &GuardsTest::handsFocusBackOnlyToDeskWindows,
        &GuardsTest::anyKnownGameOnTheTvCounts,
        &GuardsTest::launchGuardBlocksUntilSeenOrTimeout,
        &GuardsTest::runningGameIsNotRelaunched});
}

void GuardsTest::parsesActiveWindowMonitor() {
    const ActiveWindow w = parseActiveWindow(R"({"address": "0x5a", "class": "kitty", "monitor": 2})");
    CORRADE_COMPARE(w.address, "0x5a");
    CORRADE_COMPARE(w.monitorId, 2);
    CORRADE_COMPARE(parseActiveWindow("{}").address, "");
    CORRADE_COMPARE(parseActiveWindow("garbage").monitorId, -1);
}

void GuardsTest::handsFocusBackOnlyToDeskWindows() {
    CORRADE_VERIFY(shouldHandBack(ActiveWindow{"0x1", 0}, kMonitors, "HDMI-A-3"));
    // the couch launcher (or anything) on the TV: focusing it would un-fullscreen the game
    CORRADE_VERIFY(!shouldHandBack(ActiveWindow{"0x2", 1}, kMonitors, "HDMI-A-3"));
    CORRADE_VERIFY(!shouldHandBack(ActiveWindow{"", 0}, kMonitors, "HDMI-A-3")); // nothing focused
}

void GuardsTest::anyKnownGameOnTheTvCounts() {
    const std::vector<std::string> games{"alpha", "beta"};
    CORRADE_VERIFY(gameOnOutput({{"alpha", "HDMI-A-3"}, {"kitty", "DP-1"}}, games, "HDMI-A-3"));
    CORRADE_VERIFY(!gameOnOutput({{"alpha", "DP-1"}}, games, "HDMI-A-3"));      // running on the desk
    CORRADE_VERIFY(!gameOnOutput({{"vibe-launcher", "HDMI-A-3"}}, games, "HDMI-A-3")); // not a game
}

void GuardsTest::launchGuardBlocksUntilSeenOrTimeout() {
    LaunchGuard guard;
    CORRADE_VERIFY(guard.tryLaunch("alpha", 0.0));
    CORRADE_VERIFY(!guard.tryLaunch("alpha", 0.4)); // double click
    CORRADE_VERIFY(guard.pending("alpha", 0.4));
    CORRADE_VERIFY(guard.tryLaunch("beta", 0.4));    // other games unaffected
    guard.seen("alpha");                             // window mapped
    CORRADE_VERIFY(!guard.pending("alpha", 0.5));
    CORRADE_VERIFY(guard.tryLaunch("alpha", 0.5));
    CORRADE_VERIFY(guard.tryLaunch("alpha", 0.5 + LaunchGuard::kTimeout + 0.1)); // never appeared
}

void GuardsTest::runningGameIsNotRelaunched() {
    LaunchGuard guard;
    // a game already running (e.g. started from the desk) is not launched again from the couch
    CORRADE_VERIFY(!guard.tryLaunchIfStopped("alpha", true, 0.0));
    CORRADE_VERIFY(guard.tryLaunchIfStopped("alpha", false, 0.0));
}

CORRADE_TEST_MAIN(GuardsTest)
