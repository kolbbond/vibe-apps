#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace vibe::launcher {

using FileTime = std::filesystem::file_time_type;

// last-modified time; nullopt when the file is missing
inline std::optional<FileTime> modifiedTime(const std::string& path) {
    std::error_code ec;
    const FileTime t = std::filesystem::last_write_time(path, ec);
    if(ec) return std::nullopt;
    return t;
}

// reports each change of a file's modification time (disappearing and reappearing count)
class FileWatch {
public:
    explicit FileWatch(std::string path) : _path{std::move(path)}, _last{modifiedTime(_path)} {}

    bool changed() {
        const std::optional<FileTime> now = modifiedTime(_path);
        if(now == _last) return false;
        _last = now;
        return true;
    }

private:
    std::string _path;
    std::optional<FileTime> _last;
};

// remembers each running game's binary time from when it started (or was first seen
// running) and reports when the binary on disk is newer: a rebuild the game hasn't picked up
class UpdateTracker {
public:
    bool updated(const std::string& name, std::optional<FileTime> binaryTime) {
        if(!binaryTime) return false;
        const auto [it, inserted] = _started.emplace(name, *binaryTime);
        return !inserted && *binaryTime > it->second;
    }
    void started(const std::string& name, std::optional<FileTime> binaryTime) {
        if(binaryTime)
            _started[name] = *binaryTime;
        else
            _started.erase(name);
    }
    void stopped(const std::string& name) { _started.erase(name); }

private:
    std::map<std::string, FileTime> _started;
};

} // namespace vibe::launcher
