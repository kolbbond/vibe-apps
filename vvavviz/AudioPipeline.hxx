// Pure-computation audio pipeline (no ImGui dependency)
#pragma once

#include "AppState.hxx"
#include "VisualizerState.hxx"

namespace AudioPipeline {

// Fetch audio from the active source into vs.buffer and vs.display_buffer.
// Handles CAPTURE, LOOPBACK, and FILE_PLAYBACK modes uniformly.
void fetch_audio(AppState* state, VisualizerState& vs, int sample_rate);

// Run FFT on vs.buffer, writing results to vs.freqs and vs.mags.
void compute_fft(VisualizerState& vs, int sample_rate);

} // namespace AudioPipeline
