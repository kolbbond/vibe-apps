// File browser functionality for vvavviz
#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include "AppState.hxx"
#include "TrackList.hxx"

namespace fs = std::filesystem;

// Supported audio extensions
extern const std::vector<std::string> AUDIO_EXTENSIONS;

// Check if a file is an audio file based on extension
bool is_audio_file(const fs::path& path);

// Scan a directory for audio files
void scan_audio_files(AppState* state);

// Queue the found file `step` places from the current one; false if there is none
bool request_adjacent_track(AppState* state, int step);

// Draw the file browser ImGui window
void draw_file_browser(AppState* state, bool* p_open = nullptr);
