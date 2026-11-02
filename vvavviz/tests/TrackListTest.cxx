#include <Corrade/TestSuite/Tester.h>

#include "TrackList.hxx"

struct TrackListTest : Corrade::TestSuite::Tester {
    explicit TrackListTest();

    void nextAndPrevious();
    void stopsAtEitherEnd();
    void unlistedCurrentHasNoNeighbours();
    void emptyList();
    void stepZeroFindsCurrent();
};

TrackListTest::TrackListTest() {
    addTests({&TrackListTest::nextAndPrevious,
        &TrackListTest::stopsAtEitherEnd,
        &TrackListTest::unlistedCurrentHasNoNeighbours,
        &TrackListTest::emptyList,
        &TrackListTest::stepZeroFindsCurrent});
}

static const std::vector<std::string> album{"/m/1.wav", "/m/2.flac", "/m/3.mp3"};

void TrackListTest::nextAndPrevious() {
    CORRADE_COMPARE(adjacent_track(album, "/m/2.flac", +1), 2);
    CORRADE_COMPARE(adjacent_track(album, "/m/2.flac", -1), 0);
}

void TrackListTest::stopsAtEitherEnd() {
    CORRADE_COMPARE(adjacent_track(album, "/m/3.mp3", +1), -1); // auto-advance ends, no wrap
    CORRADE_COMPARE(adjacent_track(album, "/m/1.wav", -1), -1);
}

void TrackListTest::unlistedCurrentHasNoNeighbours() {
    // nothing loaded yet, or a file from outside the browsed folder
    CORRADE_COMPARE(adjacent_track(album, "", +1), -1);
    CORRADE_COMPARE(adjacent_track(album, "/elsewhere/1.wav", +1), -1);
}

void TrackListTest::emptyList() {
    CORRADE_COMPARE(adjacent_track({}, "/m/1.wav", +1), -1);
}

void TrackListTest::stepZeroFindsCurrent() {
    // startup uses step 0 to select the track given on the command line
    CORRADE_COMPARE(adjacent_track(album, "/m/3.mp3", 0), 2);
}

CORRADE_TEST_MAIN(TrackListTest)
