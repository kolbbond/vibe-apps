// Pure-computation audio pipeline implementation
#include "AudioPipeline.hxx"
#include "FftWindow.hxx"

#include <algorithm>
#include <cassert>

namespace AudioPipeline {

// --- Internal helper: copy raw audio into display and FFT buffers ---
static void fill_buffers(VisualizerState& vs, arma::Mat<fltp>& raw_buffer) {
    // Save stereo buffer before mono mixdown (scaled by volume)
    vs.stereo_buffer = raw_buffer * vs.volume;

    if (raw_buffer.n_rows > 1) {
        raw_buffer = arma::mean(raw_buffer, 0);
    }

    raw_buffer *= vs.volume;

    vs.display_buffer = raw_buffer;

    vs.buffer = fft_window(raw_buffer, static_cast<arma::uword>(vs.bufsize));
}

void fetch_audio(AppState* state, VisualizerState& vs, int sample_rate) {
    if (!vs.should_update) return;

    int display_size = static_cast<int>(vs.time_window_ms * sample_rate / 1000.0f);
    display_size = std::clamp(display_size, 100, 48000);
    int fetch_size = std::max(vs.bufsize, display_size);

    if (state->mode == AudioMode::CAPTURE && state->capture && state->capture->is_running()) {
        arma::Mat<fltp> raw = state->capture->peek_buffer(fetch_size);
        fill_buffers(vs, raw);
    }
#ifdef _WIN32
    else if (state->mode == AudioMode::LOOPBACK && state->loopback && state->loopback->is_running()) {
        arma::Mat<fltp> raw = state->loopback->peek_buffer(fetch_size);
        fill_buffers(vs, raw);
    }
#endif
    else if (state->mode == AudioMode::FILE_PLAYBACK && state->pb) {
        if (vs.last_progress != state->pb->progress) {
            vs.last_progress = state->pb->progress;

            arma::uword lidx = (vs.last_progress + fetch_size >= state->pb->audio_info->frames)
                ? state->pb->audio_info->frames - 1
                : static_cast<arma::uword>(vs.last_progress + fetch_size);
            assert(lidx <= static_cast<arma::uword>(state->pb->audio_info->frames));

            try {
                // the decoded track (all channels), not the playback copy, which is silent with --mute
                const arma::Mat<fltp>& source = state->audio_samples.n_cols > 0
                    ? state->audio_samples
                    : static_cast<const arma::Mat<fltp>&>(state->pb->audio_info->data);
                arma::Mat<fltp> raw = source.cols(vs.last_progress, lidx);
                fill_buffers(vs, raw);
            } catch(...) {}
        }
    }
}

void compute_fft(VisualizerState& vs, int sample_rate) {
    if (!vs.fft_enabled || !vs.should_update || vs.buffer.is_empty()) return;
    try {
        vs.fft->calc_frequencies(vs.buffer, sample_rate, 0, vs.buffer.n_cols - 1, vs.freqs, vs.mags);
    } catch(...) {}
}

} // namespace AudioPipeline
