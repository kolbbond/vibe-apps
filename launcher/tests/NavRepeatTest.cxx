#include <Corrade/TestSuite/Tester.h>

#include "NavRepeat.hxx"

using namespace vibe::launcher;

struct NavRepeatTest : Corrade::TestSuite::Tester {
    explicit NavRepeatTest();

    void pressStepsOnce();
    void holdRepeatsAfterDelay();
    void releaseAndOppositeStepImmediately();
    void smallAxisIsIgnored();
};

NavRepeatTest::NavRepeatTest() {
    addTests({&NavRepeatTest::pressStepsOnce,
        &NavRepeatTest::holdRepeatsAfterDelay,
        &NavRepeatTest::releaseAndOppositeStepImmediately,
        &NavRepeatTest::smallAxisIsIgnored});
}

void NavRepeatTest::pressStepsOnce() {
    NavRepeat nav;
    CORRADE_COMPARE(nav.update(1.0f, 0.016), 1);
    CORRADE_COMPARE(nav.update(1.0f, 0.016), 0);
    CORRADE_COMPARE(nav.update(1.0f, 0.2), 0); // 0.232 s held < 0.4 s delay
}

void NavRepeatTest::holdRepeatsAfterDelay() {
    NavRepeat nav;
    nav.update(-1.0f, 0.0);
    CORRADE_COMPARE(nav.update(-1.0f, 0.41), -1); // delay reached
    CORRADE_COMPARE(nav.update(-1.0f, 0.05), 0);
    CORRADE_COMPARE(nav.update(-1.0f, 0.08), -1); // 0.12 s repeat rate
}

void NavRepeatTest::releaseAndOppositeStepImmediately() {
    NavRepeat nav;
    nav.update(1.0f, 0.0);
    CORRADE_COMPARE(nav.update(0.0f, 0.016), 0);
    CORRADE_COMPARE(nav.update(1.0f, 0.016), 1);  // pressed again
    CORRADE_COMPARE(nav.update(-1.0f, 0.016), -1); // straight to the other side
}

void NavRepeatTest::smallAxisIsIgnored() {
    NavRepeat nav;
    CORRADE_COMPARE(nav.update(0.3f, 0.016), 0);
    CORRADE_COMPARE(nav.update(-0.49f, 0.016), 0);
}

CORRADE_TEST_MAIN(NavRepeatTest)
