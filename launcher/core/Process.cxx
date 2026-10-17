#include "Process.hxx"

#include <atomic>
#include <csignal>
#include <functional>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace vibe::launcher {

namespace {

// spawn argv with stdout (and optionally stderr) on a pipe, feed each chunk to onData,
// return the exit status
int spawnAndRead(const std::vector<std::string>& argv, bool mergeStderr, const std::function<void(const char*, std::size_t)>& onData,
    const std::function<void(int)>& onSpawned = {}) {
    if(argv.empty()) return -1;
    int fds[2];
    // close-on-exec: a process spawned by another thread must not inherit our write end
    if(pipe2(fds, O_CLOEXEC) != 0) return -1;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if(mergeStderr) posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    std::vector<char*> cargv;
    for(const std::string& arg : argv) cargv.push_back(const_cast<char*>(arg.c_str()));
    cargv.push_back(nullptr);

    // own process group, so a background job can be stopped with everything it started
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    if(onSpawned) {
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);
    }

    pid_t pid = 0;
    const int err = posix_spawnp(&pid, cargv[0], &actions, &attributes, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if(err == 0 && onSpawned) onSpawned(pid);
    close(fds[1]);
    if(err != 0) {
        close(fds[0]);
        return -1;
    }

    char buffer[4096];
    for(ssize_t n; (n = read(fds[0], buffer, sizeof buffer)) > 0;) onData(buffer, static_cast<std::size_t>(n));
    close(fds[0]);

    int status = 0;
    if(waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

ProcessResult runProcess(const std::vector<std::string>& argv, bool mergeStderr) {
    ProcessResult result;
    // a missing program makes posix_spawnp itself fail (glibc), giving -1
    result.status = spawnAndRead(argv, mergeStderr, [&](const char* data, std::size_t size) { result.output.append(data, size); });
    return result;
}

BackgroundJob::~BackgroundJob() {
    // closing the launcher mid-build stops the build instead of waiting for it
    // whichever of this and the spawn happens second sends the signal (seq_cst atomics)
    _stopRequested = true;
    if(const int group = _group.load(); _running && group > 0) kill(-group, SIGTERM);
    if(_thread.joinable()) _thread.join();
}

bool BackgroundJob::start(std::vector<std::string> argv) {
    if(_running) return false;
    if(_thread.joinable()) _thread.join();
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _lines.clear();
        _result.reset();
    }
    _running = true;
    _thread = std::thread{[this, command = std::move(argv)] {
        std::string partial;
        const int status = spawnAndRead(command, true, [&](const char* data, std::size_t size) {
            partial.append(data, size);
            for(std::size_t nl; (nl = partial.find('\n')) != std::string::npos;) {
                std::lock_guard<std::mutex> lock{_mutex};
                _lines.push_back(partial.substr(0, nl));
                if(_lines.size() > kMaxLines) _lines.pop_front();
                partial.erase(0, nl + 1);
            }
        }, [this](int pid) {
            _group = pid;
            if(_stopRequested) kill(-pid, SIGTERM);
        });
        std::lock_guard<std::mutex> lock{_mutex};
        if(!partial.empty()) _lines.push_back(partial);
        _lines.push_back(status == 0 ? "[build ok]" : "[build failed: exit " + std::to_string(status) + "]");
        while(_lines.size() > kMaxLines) _lines.pop_front();
        _result = status;
        _group = 0;
        _running = false;
    }};
    return true;
}

std::optional<int> BackgroundJob::takeResult() {
    std::lock_guard<std::mutex> lock{_mutex};
    std::optional<int> result = _result;
    _result.reset();
    return result;
}

std::vector<std::string> BackgroundJob::log() const {
    std::lock_guard<std::mutex> lock{_mutex};
    return {_lines.begin(), _lines.end()};
}

} // namespace vibe::launcher
