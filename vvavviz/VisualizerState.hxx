// Centralized state for the audio visualizer
#pragma once

#include <vector>
#include <armadillo>

#include "vibe/audio/FFT.hxx"
#include "vibe/graphics/FFTView.hxx"
#include "vibe/graphics/SHBlobView.hxx"
#include "vibe/graphics/SHBlob3DView.hxx"
#include "vibe/graphics/SpectralView.hxx"
#include "vibe/graphics/WaterfallView.hxx"

#include "PlasmaView.hxx"

using fltp = double;

struct VisualizerState {
    // --- View toggles ---
    bool show_spectrum_analyzer = true;
    bool show_settings = true;
    bool use_classic_view = true;
    bool show_profiling = true;

    // --- FFT config ---
    int fft_size_idx = 4;  // Index into fft_sizes (default 16384)
    static constexpr int fft_sizes[] = { 1024, 2048, 4096, 8192, 16384, 32768 };
    float update_interval_ms = 0.0f;  // 0 = continuous
    float volume = 1.0f;              // Gain (0.0 to 2.0)
    bool fft_enabled = true;
    bool use_fftw_direct = true;
    float time_window_ms = 300.0f;    // Waveform display window

    // --- FFT engine and buffers ---
    vibe::daw::ShFFTPr fft = vibe::daw::FFT::create();
    int last_progress = 0;
    int bufsize = fft_sizes[4];  // Matches fft_size_idx default
    arma::Mat<fltp> buffer{1, 16384, arma::fill::zeros};
    arma::Mat<fltp> display_buffer{1, 48000, arma::fill::zeros};
    arma::Mat<fltp> stereo_buffer;  // raw multi-channel before mono mix
    arma::Row<fltp> freqs{16384, arma::fill::zeros};
    arma::Mat<fltp> mags{1, 16384, arma::fill::zeros};

    // --- Update timing ---
    double last_update_time = 0.0;
    bool should_update = true;

    // --- Profiling (smoothed timers) ---
    float time_audio_fetch_ms = 0.0f;
    float time_fft_ms = 0.0f;
    float time_analysis_ms = 0.0f;
    float time_render_ms = 0.0f;
    float time_total_ms = 0.0f;

    // Profiling history (circular buffer)
    static constexpr int PROFILE_HISTORY_SIZE = 300;
    float history_audio[PROFILE_HISTORY_SIZE] = {};
    float history_fft[PROFILE_HISTORY_SIZE] = {};
    float history_analysis[PROFILE_HISTORY_SIZE] = {};
    float history_render[PROFILE_HISTORY_SIZE] = {};
    float history_total[PROFILE_HISTORY_SIZE] = {};
    int history_idx = 0;

    // --- VU meter ---
    float peak_hold = -60.0f;
    float peak_hold_timer = 0.0f;
    float smoothed_rms_db = -60.0f;

    // --- Reusable widgets ---
    vibe::gfx::FFTView fft_view;
    vibe::gfx::SpectralView spectral_view;

    // --- SH Blob ---
    bool show_sh_blob_2d = false;
    bool show_sh_blob_3d = false;
    vibe::gfx::SHBlobView sh_blob_view;
    vibe::gfx::SHBlob3DView sh_blob_3d_view;

    // --- Waterfall ---
    bool show_waterfall = false;
    bool show_waterfall_waveform = true;
    vibe::gfx::WaterfallView waterfall_view;
    vibe::gfx::WaterfallView waterfall_waveform_view;

    // 5-band audio (raw + smoothed)
    float sub = 0.0f, kick = 0.0f, mid5 = 0.0f, snare = 0.0f, hat = 0.0f;
    float sub_smooth = 0.0f, kick_smooth = 0.0f, mid5_smooth = 0.0f;
    float snare_smooth = 0.0f, hat_smooth = 0.0f;
    float band_smoothing = 0.8f;

    // --- Fluid plasma ---
    PlasmaView plasma;

};
