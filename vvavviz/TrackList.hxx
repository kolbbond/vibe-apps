// Track list navigation for vvavviz (header-only, no GUI/audio deps, so it is unit tested)
#pragma once

#include <algorithm>
#include <string>
#include <vector>

// Index of the file `step` places from `current` in `files`, or -1 (current not listed, or past either end)
inline int adjacent_track(const std::vector<std::string>& files, const std::string& current, int step) {
    auto it = std::find(files.begin(), files.end(), current);
    if (it == files.end()) return -1;
    const long idx = (it - files.begin()) + step;
    return (idx >= 0 && idx < static_cast<long>(files.size())) ? static_cast<int>(idx) : -1;
}
