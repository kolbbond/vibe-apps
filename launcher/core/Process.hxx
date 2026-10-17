#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vibe::launcher {

struct ProcessResult {
    int status = -1; // exit code; -1 if it could not run or did not exit normally
    std::string output;
};

// run argv (PATH lookup, no shell) and wait; captures stdout, and stderr when mergeStderr
ProcessResult runProcess(const std::vector<std::string>& argv, bool mergeStderr = false);

// one command at a time in the background (the rebuild), keeping the last lines of output
class BackgroundJob {
public:
    static constexpr std::size_t kMaxLines = 200;

    BackgroundJob() = default;
    ~BackgroundJob();
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;

    bool start(std::vector<std::string> argv); // false while a job is running
    bool running() const { return _running; }
    std::optional<int> takeResult();           // exit status, once, after the job finished
    std::vector<std::string> log() const;

private:
    mutable std::mutex _mutex;
    std::deque<std::string> _lines;
    std::optional<int> _result;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<int> _group{0}; // process group of the running job
    std::atomic<bool> _stopRequested{false};
};

} // namespace vibe::launcher
