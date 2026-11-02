// vvavviz - Audio Visualizer with FFT
// Main entry point

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <boost/filesystem.hpp>

#include <imgui.h>
#include <Magnum/GL/Renderer.h>
#include <tclap/CmdLine.h>

#include "smg/GuiBase.hh"
#include "vibe/common/Log.hxx"

#include "AppState.hxx"
#include "AudioPlayer.hxx"
#include "FileBrowser.hxx"
#include "GuiViews.hxx"

// GUI callback
int gui_callback(void* data) {
    init_docking();

    // Black background (override SMG's blue-gray default)
    Magnum::GL::Renderer::setClearColor({0.0f, 0.0f, 0.0f, 1.0f});

    AppState* state = reinterpret_cast<AppState*>(data);
    draw_gui(state);
    return 0;
}

int main(int argc, char** argv) {
    vibe::ShLogPr lg = vibe::Log::create();

    // Command line parsing
    TCLAP::CmdLine cmd("vvavviz - Audio Visualizer with FFT", ' ', "1.0");
    TCLAP::UnlabeledValueArg<std::string> file_arg("file", "Audio file to play, or a folder to play through", false, "", "filepath", cmd);
    TCLAP::SwitchArg input_arg("i", "input", "Capture from input device instead of file", cmd, false);
    TCLAP::SwitchArg list_arg("l", "list-devices", "List available input devices and exit", cmd, false);
    TCLAP::ValueArg<int> device_arg("d", "device", "Input device index (use -l to list, -1 for system default)", false, -1, "index", cmd);
    TCLAP::SwitchArg loopback_arg("", "loopback", "Windows: live capture of desktop audio from the default output device", cmd, false);
    TCLAP::SwitchArg plasma_arg("", "plasma", "Start in the fluid plasma view, full-screen background", cmd, false);
    TCLAP::SwitchArg waveform_arg("", "waveform", "Start in the waveform waterfall (history), full-screen background", cmd, false);
    TCLAP::ValueArg<std::string> preset_arg("", "preset", "Plasma preset name (neon, lava, aurora) or .json path", false, "", "preset", cmd);
    TCLAP::ValueArg<std::string> display_arg("", "display", "Plasma display: flat, terrain or planet", false, "", "mode", cmd);
    TCLAP::SwitchArg mute_arg("", "mute", "File playback: analyze the track but play silence", cmd, false);
    TCLAP::ValueArg<std::string> screenshot_arg("", "screenshot", "Write frame N (before the ImGui pass) to this .ppm and quit", false, "", "file.ppm", cmd);
    TCLAP::ValueArg<int> frames_arg("", "frames", "Frame number for --screenshot", false, 300, "n", cmd);

    cmd.parse(argc, argv);

    // Create libsoundio
    vibe::daw::ShLibSoundIoPr lsio = vibe::daw::LibSoundIo::create();
#ifdef _WIN32
    SoundIoBackend backend = SoundIoBackendWasapi;
#else
    SoundIoBackend backend = SoundIoBackendPulseAudio;
#endif
    lsio->set_backend(backend);
    lsio->init();

    // Handle --list-devices
    if (list_arg.getValue()) {
        list_devices(lsio);
        lsio->destroy();
        return 0;
    }

    // Determine mode
    AudioMode mode;
    if (input_arg.getValue() || loopback_arg.getValue()) {
        mode = AudioMode::CAPTURE;
    } else if (!file_arg.getValue().empty()) {
        mode = AudioMode::FILE_PLAYBACK;
    } else {
        std::printf("No file specified, defaulting to input capture mode.\n");
        std::printf("Use -l to list available input devices.\n\n");
        mode = AudioMode::CAPTURE;
    }

    // Create GUI
    smg::GuiConfig gui_config;
    gui_config.title = "vvavviz";
    gui_config.samples = 8; // window MSAA (plasma + background waterfall); smg falls back if unsupported
#ifndef _WIN32
    // "virtual" DPI scaling reads the X resource Xft.dpi, which Hyprland/XWayland don't set: Magnum warns
    // and falls back to physical DPI anyway, so ask for physical directly (an explicit setting still wins)
    setenv("MAGNUM_DPI_SCALING", "physical", 0);
#endif
    smg::GuiBase gb({ argc, argv }, gui_config);

    // Set window to half-width, full-height
#ifdef _WIN32
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    gb.set_window_size(screen_width / 2, screen_height);
#else
    gb.set_window_size(960, 1080);  // Reasonable default for Linux
#endif
    gb.setWindowTitle("vvavviz");

    // Enable docking BEFORE any NewFrame() call
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Set imgui.ini to user-writable location
    static std::string ini_path;
    namespace fs = boost::filesystem;
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        fs::path config_dir = fs::path(appdata) / "vvavviz";
        fs::create_directories(config_dir);
        ini_path = (config_dir / "imgui.ini").string();
        io.IniFilename = ini_path.c_str();
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        fs::path config_dir = fs::path(home) / ".config" / "vvavviz";
        fs::create_directories(config_dir);
        ini_path = (config_dir / "imgui.ini").string();
        io.IniFilename = ini_path.c_str();
    }
#endif

    smg::ShDrawCallbackPr mycb = smg::DrawCallback::create();
    mycb->set_callback(gui_callback);
    gb.add_callback(mycb);

    // Prepare application state
    AppState state;
    state.lsio = lsio;
    state.mode = mode;
    state.start_plasma = plasma_arg.getValue();
    state.start_waveform = waveform_arg.getValue();
    state.start_preset = preset_arg.getValue();
    state.start_display = display_arg.getValue();
    state.mute = mute_arg.getValue();
    state.screenshot_path = screenshot_arg.getValue();
    state.screenshot_frames = std::max(1, frames_arg.getValue());

    SoundIo* soundio = lsio->get_soundio();

    if (mode == AudioMode::FILE_PLAYBACK) {
        // File playback mode
        // the browser lists the file's folder (or the folder given), so Next/auto-advance continue through it
        std::string fpath = file_arg.getValue();
        const bool is_dir = fs::is_directory(fpath);
        state.search_path = is_dir ? fpath : fs::absolute(fpath).parent_path().string();
        scan_audio_files(&state);
        if (is_dir) {
            if (state.found_files.empty()) {
                std::printf("No audio files in: %s\n", fpath.c_str());
                lsio->destroy();
                return 1;
            }
            fpath = state.found_files.front();
        } else {
            fpath = fs::absolute(fpath).string();
        }
        state.current_file = fpath;
        state.selected_file_idx = adjacent_track(state.found_files, fpath, 0);
        lg->msg("Audio File: %s%s%s\n", KCYN, fpath.c_str(), KNRM);

        arma::Mat<fltp> samples;
        int sample_rate;
        if (!load_audio_file(fpath, samples, sample_rate)) {
            std::printf("Failed to load audio file: %s\n", fpath.c_str());
            lsio->destroy();
            return 1;
        }

        SoundIoDevice* device = lsio->get_output_device();
        SoundIoOutStream* outstream = nullptr;
        lsio->create_outstream(outstream, device);

        state.audio_samples = samples; // analysis reads the decoded track
        state.waves.push_back({ outstream->format,
            state.mute ? arma::Row<fltp>(samples.n_cols, arma::fill::zeros) : arma::Row<fltp>(samples.row(0)),
            static_cast<int>(samples.n_cols) });
        state.pb = new vibe::daw::PlayBackInfo({ &state.waves[0], 0, false, soundio, lsio->_write_sample });
        state.pb->loop = false;
        state.os = outstream;
        state.sample_rate = sample_rate;
        state.output_device = device;

        // Setup outstream (the one created above: state.os owns it, cleanup destroys it)
        outstream->userdata = state.pb;
        outstream->write_callback = vibe::daw::WavPlayer::write_callback;
        outstream->underflow_callback = vibe::daw::WavPlayer::underflow_callback;
        outstream->name = nullptr;
        outstream->software_latency = 0.050;
        outstream->sample_rate = sample_rate;
        lsio->open_outstream(outstream);

        // Start playback thread
        vibe::daw::PlayBackInfo* pi = state.pb;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        state.playback_finished = finished;
        state.playback_thread = std::thread([outstream, lsio, pi, finished]() {
            if (int err = soundio_outstream_start(outstream)) {
                fprintf(stderr, "unable to start device: %s\n", soundio_strerror(err));
                *finished = true;
                return;
            }
            while (!pi->is_done) { soundio_wait_events(lsio->get_soundio()); }
            *finished = true; // the stream is destroyed by whoever joins (stop_playback callers)
        });

        // Also set up capture for later switching
        state.capture = vibe::daw::AudioCapture::create();
        state.capture->set_libsoundio(lsio);
        state.all_devices = lsio->list_all_devices();
        state.current_input_idx = device_arg.getValue();

        lg->msg("%sPlayback Started%s\n", KGRN, KNRM);

    } else {
        // Capture mode
        state.capture = vibe::daw::AudioCapture::create();
        state.capture->set_libsoundio(lsio);
        state.all_devices = lsio->list_all_devices();
        state.output_device = lsio->get_output_device();

#ifdef _WIN32
        // Create loopback capture instance (ready for use when user selects output device)
        state.loopback = vibe::daw::WasapiLoopback::create();

        // --loopback: follow whatever the default output is right now
        if (loopback_arg.getValue()) {
            if (state.loopback->start(-1)) {
                state.sample_rate = state.loopback->get_sample_rate();
                state.mode = AudioMode::LOOPBACK;
                lg->msg("%sLoopback Started (default output)%s\n", KGRN, KNRM);
                goto skip_capture;
            }
            lg->msg("%sDefault output loopback failed, trying saved device%s\n", KYEL, KNRM);
        }

        // Check for saved loopback device preference
        if (std::string saved_loopback = load_loopback_device(); !saved_loopback.empty()) {
            lg->msg("Trying saved loopback device: %s%s%s\n", KCYN, saved_loopback.c_str(), KNRM);
            if (state.loopback->start_by_name(saved_loopback)) {
                state.current_output_name = saved_loopback;
                state.sample_rate = state.loopback->get_sample_rate();
                state.mode = AudioMode::LOOPBACK;
                lg->msg("%sLoopback Started (saved device)%s\n", KGRN, KNRM);
                goto skip_capture;  // Skip regular capture setup
            } else {
                lg->msg("%sSaved loopback device not available, falling back to capture%s\n", KYEL, KNRM);
            }
        }
#endif

        {
            int device_idx = device_arg.getValue();

            // If device_idx is -1, find the preferred device
            if (device_idx < 0) {
#ifdef _WIN32
                // On Windows, prefer Stereo Mix for system audio capture
                for (const auto& dev : state.all_devices) {
                    if (dev.type == vibe::daw::DeviceType::INPUT &&
                        dev.name.find("Stereo Mix") != std::string::npos) {
                        device_idx = dev.index;
                        lg->msg("Using Stereo Mix: %s[%d] %s%s\n", KCYN, dev.index, dev.name.c_str(), KNRM);
                        break;
                    }
                }
#endif
                // Fall back to system default if Stereo Mix not found
                if (device_idx < 0) {
                    for (const auto& dev : state.all_devices) {
                        if (dev.type == vibe::daw::DeviceType::INPUT && dev.is_default) {
                            device_idx = dev.index;
                            lg->msg("Using default input device: %s[%d] %s%s\n", KCYN, dev.index, dev.name.c_str(), KNRM);
                            break;
                        }
                    }
                }
            }

            state.current_input_idx = device_idx;

            if (!state.capture->start(device_idx)) {
                std::printf("Failed to start audio capture.\n");
                std::printf("Available devices:\n");
                list_devices(lsio);
                lsio->destroy();
                return 1;
            }

            state.sample_rate = state.capture->get_sample_rate();

            lg->msg("%sCapture Started%s\n", KGRN, KNRM);
        }
#ifdef _WIN32
        skip_capture:;
#endif
    }

    mycb->set_data(&state);

    // Main loop
    bool done = false;
    std::string advanced_from; // current_file the last auto-advance left, so a finished list doesn't retry each frame
    while (!done) {
        // Auto-advance: a finished track queues the next one in the browser's list
        if (state.auto_advance && state.mode == AudioMode::FILE_PLAYBACK && state.pb && state.pb->is_done &&
            !state.file_load_requested && state.current_file != advanced_from) {
            advanced_from = state.current_file;
            request_adjacent_track(&state, +1);
        }

        // Handle pending file load request
        if (state.file_load_requested) {
            state.file_load_requested = false;
            start_file_playback(&state, state.pending_file);
            state.pending_file.clear();
        }

        // Handle switch to live capture request
        if (state.capture_requested) {
            state.capture_requested = false;

            // Stop current playback
            if (state.pb) {
                state.pb->is_done = true;
                soundio_wakeup(soundio);
            }

            // Start capture if not already running
            if (state.capture && !state.capture->is_running()) {
                if (state.capture->start(state.current_input_idx)) {
                    state.sample_rate = state.capture->get_sample_rate();
                    state.mode = AudioMode::CAPTURE;
                    lg->msg("%sSwitched to Live Capture%s\n", KGRN, KNRM);
                }
            } else if (state.capture && state.capture->is_running()) {
                state.mode = AudioMode::CAPTURE;
                lg->msg("%sSwitched to Live Capture%s\n", KGRN, KNRM);
            }
        }

        soundio_flush_events(soundio);
        done = !gb.mainLoopIteration() || state.quit_requested;
    }

    // Cleanup: destroy the views (they own GL objects) while the GL context still exists
    release_gl_resources();
    // the playback thread waits inside soundio, so stop and join it first
    stop_playback(&state);
    if (state.os && lsio->valid_outstream(state.os)) {
        soundio_outstream_destroy(state.os);
        state.os = nullptr;
    }
    if (state.capture) {
        state.capture->stop();
    }
#ifdef _WIN32
    if (state.loopback) {
        state.loopback->stop();
    }
#endif
    // state.output_device is lsio's own device (no extra reference was taken):
    // lsio->destroy() releases it, a second unref here freed it twice
    lsio->destroy();
    delete state.pb;

#ifndef _WIN32
    // everything vvavviz owns is released above; skip SDL's teardown in ~GuiBase, whose Wayland disconnect
    // (sdl2-compat on SDL3) segfaults on some exits. ImGui would save its layout there, so save it here
    if (const char* ini = ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ini);
    std::fflush(nullptr);
    std::_Exit(0);
#endif
    return 0;
}
