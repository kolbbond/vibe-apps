// Audio player implementation
#include "AudioPlayer.hxx"

#include <cstdio>
#include <chrono>
#include <thread>

#include "vibe/audio/WavData.hxx"
#include "vibe/audio/FFmpegAudioData.hxx"
#include "vibe/audio/WavPlayer.hxx"

void list_devices(vibe::daw::ShLibSoundIoPr lsio) {
    auto all_devices = lsio->list_all_devices();
    std::printf("\nAvailable audio devices:\n");
    std::printf("-------------------------\n");
    for (const auto& dev : all_devices) {
        const char* type_str = (dev.type == vibe::daw::DeviceType::INPUT) ? "IN " : "OUT";
        std::printf("[%s %d] %s%s\n",
            type_str,
            dev.index,
            dev.name.c_str(),
            dev.is_default ? " (default)" : "");
        std::printf("         ID: %s\n", dev.id.c_str());
        if (dev.sample_rate_min > 0) {
            std::printf("         Sample rates: %d - %d Hz\n", dev.sample_rate_min, dev.sample_rate_max);
        }
    }
    std::printf("\n");
}

bool load_audio_file(const std::string& fpath, arma::Mat<fltp>& samples, int& sample_rate) {
    // Try FFmpeg first for broader format support
    if (vibe::daw::FFmpegAudioData::supports_format(fpath)) {
        auto ffmpeg_data = vibe::daw::FFmpegAudioData::create();
        if (ffmpeg_data->load(fpath)) {
            samples = ffmpeg_data->get_sample_matrix();
            sample_rate = ffmpeg_data->get_sample_rate();
            return true;
        }
    }

    // Fall back to WavData for WAV/AIFF files
    auto wav_data = vibe::daw::WavData::create();
    if (wav_data->load(fpath)) {
        samples = wav_data->get_sample_matrix();
        sample_rate = wav_data->get_sample_rate();
        return true;
    }

    return false;
}

void stop_playback(AppState* state) {
    if (state->pb) state->pb->is_done = true;
    if (!state->playback_thread.joinable()) return;
    // a wakeup is lost if the thread is between its is_done check and its wait: repeat until it exits
    while (state->playback_finished && !state->playback_finished->load()) {
        soundio_wakeup(state->lsio->get_soundio());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    state->playback_thread.join();
}

bool start_file_playback(AppState* state, const std::string& filepath) {
    std::printf("Loading file: %s\n", filepath.c_str());

    // Stop any current capture
    if (state->capture && state->capture->is_running()) {
        state->capture->stop();
    }

    // Stop any current playback
    stop_playback(state);

    // Destroy old outstream if exists
    if (state->os && state->lsio->valid_outstream(state->os)) {
        soundio_outstream_destroy(state->os);
        state->os = nullptr;
    }

    // Load the audio file
    int new_sample_rate = 44100;
    if (!load_audio_file(filepath, state->audio_samples, new_sample_rate)) {
        std::printf("Failed to load: %s\n", filepath.c_str());
        state->current_file = filepath; // so auto-advance moves past it instead of retrying it
        return false;
    }

    std::printf("Loaded: %llu samples, %d Hz\n", state->audio_samples.n_cols, new_sample_rate);

    // Get output device if needed
    if (!state->output_device) {
        state->output_device = state->lsio->get_output_device();
    }

    // Create outstream
    SoundIoOutStream* outstream = nullptr;
    state->lsio->create_outstream(outstream, state->output_device);

    // Setup waves and playback info
    state->waves.clear();
    state->waves.push_back({
        outstream->format,
        state->mute ? arma::Row<fltp>(state->audio_samples.n_cols, arma::fill::zeros)
                    : arma::Row<fltp>(state->audio_samples.row(0)),
        static_cast<int>(state->audio_samples.n_cols)
    });

    // Clean up old playback info
    if (state->pb) {
        delete state->pb;
    }

    state->pb = new vibe::daw::PlayBackInfo({
        &state->waves[0], 0, false,
        state->lsio->get_soundio(),
        state->lsio->_write_sample
    });
    state->pb->loop = false;

    // Configure outstream
    outstream->userdata = state->pb;
    outstream->write_callback = vibe::daw::WavPlayer::write_callback;
    outstream->underflow_callback = vibe::daw::WavPlayer::underflow_callback;
    outstream->name = nullptr;
    outstream->software_latency = 0.050;
    outstream->sample_rate = new_sample_rate;

    int err = state->lsio->open_outstream(outstream);
    if (err != 0 || !state->lsio->valid_outstream(outstream)) {
        std::printf("Failed to open outstream\n");
        state->current_file = filepath;
        return false;
    }

    state->os = outstream;
    state->sample_rate = new_sample_rate;
    state->mode = AudioMode::FILE_PLAYBACK;
    state->current_file = filepath;

    // Start playback in background thread
    vibe::daw::PlayBackInfo* pi = state->pb;
    SoundIoOutStream* os = outstream;

    auto finished = std::make_shared<std::atomic<bool>>(false);
    state->playback_finished = finished;
    state->playback_thread = std::thread([os, pi, finished]() {
        if (int err = soundio_outstream_start(os)) {
            fprintf(stderr, "Unable to start playback: %s\n", soundio_strerror(err));
            *finished = true;
            return;
        }
        // only the main loop pumps libsoundio events (see vvavviz.cxx)
        while (!pi->is_done) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::printf("Playback complete\n");
        *finished = true;
    });

    std::printf("Playback started\n");
    return true;
}
