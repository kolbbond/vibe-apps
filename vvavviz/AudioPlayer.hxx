// Audio player functionality for vvavviz
#pragma once

#include <string>
#include <vector>

#include "AppState.hxx"
#include "vibe/audio/LibSoundIo.hxx"

// List available input devices to stdout
void list_devices(vibe::daw::ShLibSoundIoPr lsio);

// Load an audio file (tries FFmpeg first, falls back to WavData)
bool load_audio_file(const std::string& fpath, arma::Mat<fltp>& samples, int& sample_rate);

// Start file playback - can be called dynamically
bool start_file_playback(AppState* state, const std::string& filepath);

// signal the playback thread, wake it until it has exited, and join it
void stop_playback(AppState* state);
