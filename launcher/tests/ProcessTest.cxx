#include <chrono>
#include <thread>

#include <Corrade/TestSuite/Tester.h>
#include <Corrade/Utility/DebugStl.h>

#include "Process.hxx"

using namespace vibe::launcher;

struct ProcessTest : Corrade::TestSuite::Tester {
    explicit ProcessTest();

    void capturesStdout();
    void reportsExitStatus();
    void stderrOnlyWhenMerged();
    void missingProgramIsMinusOne();
    void backgroundJobCollectsLinesAndStatus();
    void backgroundJobRunsOneAtATime();
    void destroyingARunningJobStopsIt();
};

ProcessTest::ProcessTest() {
    addTests({&ProcessTest::capturesStdout,
        &ProcessTest::reportsExitStatus,
        &ProcessTest::stderrOnlyWhenMerged,
        &ProcessTest::missingProgramIsMinusOne,
        &ProcessTest::backgroundJobCollectsLinesAndStatus,
        &ProcessTest::backgroundJobRunsOneAtATime,
        &ProcessTest::destroyingARunningJobStopsIt});
}

void ProcessTest::capturesStdout() {
    const ProcessResult r = runProcess({"echo", "hello world"});
    CORRADE_COMPARE(r.status, 0);
    CORRADE_COMPARE(r.output, "hello world\n");
}

void ProcessTest::reportsExitStatus() {
    CORRADE_COMPARE(runProcess({"sh", "-c", "exit 3"}).status, 3);
}

void ProcessTest::stderrOnlyWhenMerged() {
    CORRADE_COMPARE(runProcess({"sh", "-c", "echo out; echo err >&2"}).output, "out\n");
    CORRADE_COMPARE(runProcess({"sh", "-c", "echo out; echo err >&2"}, true).output, "out\nerr\n");
}

void ProcessTest::missingProgramIsMinusOne() {
    CORRADE_COMPARE(runProcess({"vibe-no-such-program-xyz"}).status, -1);
}

void ProcessTest::backgroundJobCollectsLinesAndStatus() {
    BackgroundJob job;
    CORRADE_VERIFY(job.start({"sh", "-c", "echo a; echo b; exit 2"}));
    std::optional<int> status;
    while(!(status = job.takeResult())) std::this_thread::yield();
    CORRADE_COMPARE(*status, 2);
    CORRADE_VERIFY(!job.takeResult()); // reported once
    const auto log = job.log();
    CORRADE_COMPARE(log.size(), 3u);
    CORRADE_COMPARE(log[0], "a");
    CORRADE_COMPARE(log[1], "b");
    CORRADE_COMPARE(log[2], "[build failed: exit 2]");
}

void ProcessTest::backgroundJobRunsOneAtATime() {
    BackgroundJob job;
    CORRADE_VERIFY(job.start({"sh", "-c", "sleep 0.3"})); // the child sleeps, keeping the job busy
    CORRADE_VERIFY(job.running());
    CORRADE_VERIFY(!job.start({"echo", "second"}));
    while(!job.takeResult()) std::this_thread::yield();
    CORRADE_VERIFY(!job.running());
    CORRADE_VERIFY(job.start({"echo", "again"}));
    while(!job.takeResult()) std::this_thread::yield();
}

void ProcessTest::destroyingARunningJobStopsIt() {
    const auto start = std::chrono::steady_clock::now();
    {
        BackgroundJob job;
        CORRADE_VERIFY(job.start({"sh", "-c", "sleep 30; echo late"}));
        while(!job.running()) std::this_thread::yield();
    } // closing the launcher mid-build
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CORRADE_VERIFY(seconds < 5.0);
}

CORRADE_TEST_MAIN(ProcessTest)
