#include <chrono>
#include <cstdio>
#include <fstream>

#include <unistd.h>

#include <Corrade/TestSuite/Tester.h>

#include "Watch.hxx"

using namespace vibe::launcher;
namespace fs = std::filesystem;

namespace {
std::string tempFile(const char* name) {
    const fs::path p = fs::temp_directory_path() / (std::string{"vibe-watch-"} + name + "-" + std::to_string(::getpid()));
    std::ofstream{p} << "x";
    return p.string();
}
void bump(const std::string& path, int seconds) {
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds{seconds});
}
} // namespace

struct WatchTest : Corrade::TestSuite::Tester {
    explicit WatchTest();

    void unchangedFileIsQuiet();
    void touchIsReportedOnce();
    void removalAndReappearanceAreChanges();
    void trackerBaselinesOnFirstSight();
    void trackerFlagsNewerBinary();
    void startedResetsBaseline();
    void stoppedForgetsBaseline();
    void missingBinaryIsNeverUpdated();
};

WatchTest::WatchTest() {
    addTests({&WatchTest::unchangedFileIsQuiet,
        &WatchTest::touchIsReportedOnce,
        &WatchTest::removalAndReappearanceAreChanges,
        &WatchTest::trackerBaselinesOnFirstSight,
        &WatchTest::trackerFlagsNewerBinary,
        &WatchTest::startedResetsBaseline,
        &WatchTest::stoppedForgetsBaseline,
        &WatchTest::missingBinaryIsNeverUpdated});
}

void WatchTest::unchangedFileIsQuiet() {
    const std::string p = tempFile("quiet");
    FileWatch watch{p};
    CORRADE_VERIFY(!watch.changed());
    CORRADE_VERIFY(!watch.changed());
    fs::remove(p);
}

void WatchTest::touchIsReportedOnce() {
    const std::string p = tempFile("touch");
    FileWatch watch{p};
    bump(p, 1);
    CORRADE_VERIFY(watch.changed());
    CORRADE_VERIFY(!watch.changed());
    fs::remove(p);
}

void WatchTest::removalAndReappearanceAreChanges() {
    const std::string p = tempFile("gone");
    FileWatch watch{p};
    fs::remove(p);
    CORRADE_VERIFY(watch.changed());
    CORRADE_VERIFY(!watch.changed());
    std::ofstream{p} << "back";
    CORRADE_VERIFY(watch.changed());
    fs::remove(p);
}

void WatchTest::trackerBaselinesOnFirstSight() {
    UpdateTracker tracker;
    const FileTime t = FileTime::clock::now();
    CORRADE_VERIFY(!tracker.updated("alpha", t)); // running when the launcher started
    CORRADE_VERIFY(!tracker.updated("alpha", t));
}

void WatchTest::trackerFlagsNewerBinary() {
    UpdateTracker tracker;
    const FileTime t = FileTime::clock::now();
    tracker.updated("alpha", t);
    CORRADE_VERIFY(tracker.updated("alpha", t + std::chrono::seconds{5}));
}

void WatchTest::startedResetsBaseline() {
    UpdateTracker tracker;
    const FileTime t = FileTime::clock::now();
    tracker.updated("alpha", t);
    tracker.started("alpha", t + std::chrono::seconds{5}); // relaunched on the new build
    CORRADE_VERIFY(!tracker.updated("alpha", t + std::chrono::seconds{5}));
}

void WatchTest::stoppedForgetsBaseline() {
    UpdateTracker tracker;
    const FileTime t = FileTime::clock::now();
    tracker.updated("alpha", t);
    tracker.stopped("alpha");
    CORRADE_VERIFY(!tracker.updated("alpha", t + std::chrono::seconds{5})); // new baseline
}

void WatchTest::missingBinaryIsNeverUpdated() {
    UpdateTracker tracker;
    CORRADE_VERIFY(!tracker.updated("ghost", std::nullopt));
    CORRADE_VERIFY(!tracker.updated("ghost", std::nullopt));
}

CORRADE_TEST_MAIN(WatchTest)
