// Application state and types for vvavviz
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <armadillo>
#include <soundio/soundio.h>
#include <boost/filesystem.hpp>

#include "vibe/audio/LibSoundIo.hxx"
#include "vibe/audio/AudioCapture.hxx"
#include "vibe/audio/WavPlayer.hxx"

#ifdef _WIN32
#include "vibe/audio/WasapiLoopback.hxx"
#endif

// Get config directory path
inline std::string get_config_dir() {
    namespace fs = boost::filesystem;
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        fs::path config_dir = fs::path(appdata) / "vvavviz";
        fs::create_directories(config_dir);
        return config_dir.string();
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        fs::path config_dir = fs::path(home) / ".config" / "vvavviz";
        fs::create_directories(config_dir);
        return config_dir.string();
    }
#endif
    return ".";
}

// Save preferred loopback device name
inline void save_loopback_device(const std::string& device_name) {
    namespace fs = boost::filesystem;
    fs::path config_file = fs::path(get_config_dir()) / "loopback_device.txt";
    std::ofstream out(config_file.string());
    if (out) {
        out << device_name;
    }
}

// Load preferred loopback device name
inline std::string load_loopback_device() {
    namespace fs = boost::filesystem;
    fs::path config_file = fs::path(get_config_dir()) / "loopback_device.txt";
    std::ifstream in(config_file.string());
    std::string device_name;
    if (in) {
        std::getline(in, device_name);
    }
    return device_name;
}

using fltp = double;

// Mode of operation
enum class AudioMode {
    FILE_PLAYBACK,  // Playing a file
    CAPTURE,        // Capturing from input device
    LOOPBACK        // Capturing from output device (WASAPI loopback)
};

// Application state passed to GUI callback
struct AppState {
    vibe::daw::PlayBackInfo* pb = nullptr;
    // file playback's soundio wait loop; joined by stop_playback() before soundio goes away
    std::thread playback_thread;
    std::shared_ptr<std::atomic<bool>> playback_finished;
    SoundIoOutStream* os = nullptr;
    vibe::daw::ShLibSoundIoPr lsio = nullptr;
    vibe::daw::ShAudioCapturePr capture = nullptr;
#ifdef _WIN32
    vibe::daw::ShWasapiLoopbackPr loopback = nullptr;
#endif
    AudioMode mode = AudioMode::CAPTURE;
    int sample_rate = 44100;
    std::vector<vibe::daw::AudioDeviceInfo> all_devices;
    int current_input_idx = -1;
    int current_output_idx = -1;
    std::string current_output_name = "";  // For loopback device persistence

    // File browser state
    std::string search_path = "";
    std::vector<std::string> found_files;
    int selected_file_idx = -1;
    bool needs_refresh = true;
    std::string current_file = "";

    // Dynamic file loading
    std::string pending_file = "";
    bool file_load_requested = false;

    // Switch to live capture
    bool capture_requested = false;

    // command line: plasma playground and automated screenshots
    bool start_plasma = false;
    bool start_waveform = false;     // waveform waterfall (history) as the full-screen background
    std::string start_preset = "";
    std::string start_display = ""; // flat, terrain or planet (after the preset)
    bool mute = false;               // file playback: analyze the track but play silence
    std::string screenshot_path = "";
    int screenshot_frames = 300;
    bool quit_requested = false;

    // Audio data storage for file playback
    arma::Mat<fltp> audio_samples;
    std::vector<vibe::daw::AudioInfo> waves;
    SoundIoDevice* output_device = nullptr;
};
