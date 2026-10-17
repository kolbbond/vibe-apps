// GUI views implementation
#include "GuiViews.hxx"
#include "VisualizerState.hxx"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <smg/implot.h>
#include <smg/implot3d.h>
#include "AudioPipeline.hxx"
#include "FileBrowser.hxx"
#include "vibe/audio/SpectralAnalysis.hxx"

#include <Corrade/Containers/StridedArrayView.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/Image.h>
#include <Magnum/Math/Color.h>
#include <Magnum/PixelFormat.h>

static bool show_visualizer = true;
static bool show_music_browser = true;

void init_docking() {
    static bool initialized = false;
    if (!initialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // Initialize ImPlot3D context
        ImPlot3D::CreateContext();

        initialized = true;
    }
    // Note: Default dock layout is saved in imgui.ini
    // User can drag windows to dock them, layout persists across sessions
}

// ---------------------------------------------------------------------------
// Extracted rendering functions
// ---------------------------------------------------------------------------

static void draw_menu_bar(VisualizerState& vs) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Audio Visualizer##menu", nullptr, &show_visualizer);
            ImGui::MenuItem("Music Browser##menu", nullptr, &show_music_browser);
            ImGui::Separator();
            ImGui::MenuItem("Classic FFT##menu", nullptr, &vs.use_classic_view);
            ImGui::Separator();
            ImGui::MenuItem("Spectrum Analyzer##menu", nullptr, &vs.show_spectrum_analyzer);
            ImGui::MenuItem("Spectrogram##menu", nullptr, &vs.spectral_view.show_spectrogram);
            ImGui::MenuItem("Spectral Flux##menu", nullptr, &vs.spectral_view.show_spectral_flux);
            ImGui::MenuItem("Chromagram##menu", nullptr, &vs.spectral_view.show_chromagram);
            ImGui::Separator();
            ImGui::MenuItem("SH Blob 2D##menu", nullptr, &vs.show_sh_blob_2d);
            ImGui::MenuItem("SH Blob 3D##menu", nullptr, &vs.show_sh_blob_3d);
            ImGui::Separator();
            ImGui::MenuItem("Waterfall (FFT)##menu", nullptr, &vs.show_waterfall);
            ImGui::MenuItem("Waterfall (Waveform)##menu", nullptr, &vs.show_waterfall_waveform);
            ImGui::Separator();
            ImGui::MenuItem("Plasma##menu", nullptr, &vs.plasma.show);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            ImGui::MenuItem("Show Settings Panel##menu", nullptr, &vs.show_settings);
            ImGui::MenuItem("Show Profiling##menu", nullptr, &vs.show_profiling);
            ImGui::Separator();
            const char* fft_size_labels[] = { "1024", "2048", "4096", "8192", "16384", "32768" };
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("FFT Size##mainmenu", &vs.fft_size_idx, fft_size_labels, 6)) {}
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Update Interval##mainmenu", &vs.update_interval_ms, 0.0f, 500.0f,
                vs.update_interval_ms == 0.0f ? "Continuous" : "%.0f ms");
            ImGui::EndMenu();
        }
        // Volume control in menu bar
        ImGui::Separator();
        ImGui::Text("Vol:");
        ImGui::SetNextItemWidth(80);
        ImGui::SliderFloat("##volume", &vs.volume, 0.0f, 2.0f, "%.1f");
        ImGui::Separator();
        ImGui::Checkbox("FFT", &vs.fft_enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Direct FFTW", &vs.use_fftw_direct);
        ImGui::EndMainMenuBar();
    }
}

static void draw_status_line(AppState* state, int sample_rate) {
    float fps = ImGui::GetIO().Framerate;
    ImGui::Text("%.1f fps (%.1f ms)", fps, 1000.0f / fps);
    ImGui::SameLine();
    if (state->mode == AudioMode::CAPTURE) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "LIVE");
    } else if (state->mode == AudioMode::LOOPBACK) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "LOOPBACK");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1, 1), "FILE");
        ImGui::SameLine();
        if (ImGui::Button("Live")) {
            state->capture_requested = true;
        }
    }
    ImGui::SameLine();
    ImGui::Text("%d Hz", sample_rate);
}

static void draw_vu_meter(VisualizerState& vs) {
    auto levels = vibe::daw::compute_levels(vs.buffer);
    float rms_db = levels.rms_db;
    float peak_db = levels.peak_db;

    // Smooth the RMS display
    vs.smoothed_rms_db = 0.8f * vs.smoothed_rms_db + 0.2f * rms_db;

    // Peak hold with decay
    if (peak_db > vs.peak_hold) {
        vs.peak_hold = peak_db;
        vs.peak_hold_timer = 1.0f;  // Hold for ~1 second
    } else {
        vs.peak_hold_timer -= ImGui::GetIO().DeltaTime;
        if (vs.peak_hold_timer <= 0.0f) {
            vs.peak_hold -= 30.0f * ImGui::GetIO().DeltaTime;  // Decay rate
            vs.peak_hold = std::max(-60.0f, vs.peak_hold);
        }
    }

    // Draw level meter
    ImGui::Separator();
    ImGui::Text("Level:");
    ImGui::SameLine();

    // Normalize to 0-1 for progress bar (map -60dB to 0dB -> 0 to 1)
    float rms_norm = (vs.smoothed_rms_db + 60.0f) / 60.0f;
    float peak_norm = (vs.peak_hold + 60.0f) / 60.0f;

    // Color based on level: green -> yellow -> red
    ImVec4 meter_color;
    if (vs.smoothed_rms_db < -12.0f) {
        meter_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // Green
    } else if (vs.smoothed_rms_db < -3.0f) {
        meter_color = ImVec4(0.9f, 0.9f, 0.2f, 1.0f);  // Yellow
    } else {
        meter_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // Red (near clipping)
    }

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, meter_color);
    ImGui::ProgressBar(rms_norm, ImVec2(200, 16), "");
    ImGui::PopStyleColor();

    // Draw peak marker on the meter
    ImVec2 bar_pos = ImGui::GetItemRectMin();
    ImVec2 bar_size = ImGui::GetItemRectSize();
    float peak_x = bar_pos.x + peak_norm * bar_size.x;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(peak_x, bar_pos.y),
        ImVec2(peak_x, bar_pos.y + bar_size.y),
        IM_COL32(255, 255, 255, 255), 2.0f);

    // Show dB values
    ImGui::SameLine();
    ImGui::Text("%.1f dB", vs.smoothed_rms_db);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(peak: %.1f)", vs.peak_hold);

    // Draw scale markers
    ImGui::Text("       -60    -48    -36    -24    -12     -6     0 dB");
}

static void draw_device_selector(AppState* state) {
    bool is_capture_mode = (state->mode == AudioMode::CAPTURE || state->mode == AudioMode::LOOPBACK);
    if (!is_capture_mode) return;

    if (ImGui::CollapsingHeader("Audio Devices")) {
        // Refresh button to re-enumerate devices
        if (ImGui::Button("Refresh Devices")) {
            state->all_devices = state->lsio->list_all_devices();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(%zu devices)", state->all_devices.size());

        static int selected_device_idx = -1;
        static bool selected_is_output = false;

        // Initialize selection from current state
        if (selected_device_idx < 0) {
            if (state->mode == AudioMode::LOOPBACK) {
                selected_device_idx = state->current_output_idx;
                selected_is_output = true;
            } else {
                selected_device_idx = state->current_input_idx;
                selected_is_output = false;
            }
        }

        // Find current device name for preview
        std::string preview_text = "Select device...";
        for (const auto& dev : state->all_devices) {
            bool is_output = (dev.type == vibe::daw::DeviceType::OUTPUT);
            if (is_output == selected_is_output && dev.index == selected_device_idx) {
                preview_text = (is_output ? "[OUT] " : "[IN] ") + dev.name;
                break;
            }
        }

        ImGui::SetNextItemWidth(500);
        if (ImGui::BeginCombo("Device##audiodevice", preview_text.c_str())) {
            // Show all devices with type prefix
            for (size_t i = 0; i < state->all_devices.size(); i++) {
                const auto& dev = state->all_devices[i];
                bool is_output = (dev.type == vibe::daw::DeviceType::OUTPUT);
                bool is_selected = (is_output == selected_is_output) && (dev.index == selected_device_idx);

                std::string type_prefix = is_output ? "[OUT] " : "[IN]  ";
                std::string label = type_prefix + dev.name;
                if (dev.is_default) label += " (default)";
#ifdef _WIN32
                if (is_output) label += " (loopback)";
#endif
                // Add unique ID to prevent ImGui ID clashing
                label += "##device" + std::to_string(i);

#ifdef _WIN32
                // On Windows, both input and output devices are selectable
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    bool device_changed = (selected_device_idx != dev.index) || (selected_is_output != is_output);
                    if (device_changed) {
                        selected_device_idx = dev.index;
                        selected_is_output = is_output;

                        if (is_output) {
                            // Switch to loopback mode
                            if (state->capture && state->capture->is_running()) {
                                state->capture->stop();
                            }
                            if (!state->loopback) {
                                state->loopback = vibe::daw::WasapiLoopback::create();
                            }
                            if (state->loopback->is_running()) {
                                state->loopback->stop();
                            }
                            // Use device name for loopback (survives reboots/reconnects)
                            if (state->loopback->start_by_name(dev.name)) {
                                state->current_output_idx = dev.index;
                                state->current_output_name = dev.name;
                                state->sample_rate = state->loopback->get_sample_rate();
                                state->mode = AudioMode::LOOPBACK;
                                // Save preference for next startup
                                save_loopback_device(dev.name);
                            }
                        } else {
                            // Switch to regular capture mode
                            if (state->loopback && state->loopback->is_running()) {
                                state->loopback->stop();
                            }
                            if (state->capture) {
                                if (state->capture->is_running()) {
                                    state->capture->stop();
                                }
                                if (state->capture->start(dev.index)) {
                                    state->current_input_idx = dev.index;
                                    state->sample_rate = state->capture->get_sample_rate();
                                    state->mode = AudioMode::CAPTURE;
                                    // Clear loopback preference when switching to input
                                    save_loopback_device("");
                                }
                            }
                        }
                    }
                }
#else
                // On non-Windows, only input devices are selectable
                if (!is_output) {
                    if (ImGui::Selectable(label.c_str(), is_selected)) {
                        if (selected_device_idx != dev.index || selected_is_output) {
                            selected_device_idx = dev.index;
                            selected_is_output = false;
                            if (state->capture) {
                                state->capture->stop();
                                if (state->capture->start(dev.index)) {
                                    state->current_input_idx = dev.index;
                                    state->sample_rate = state->capture->get_sample_rate();
                                }
                            }
                        }
                    }
                } else {
                    // Output devices shown but grayed out on non-Windows
                    ImGui::TextDisabled("%s", label.c_str());
                }
#endif
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
#ifdef _WIN32
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Tip: Select [OUT] devices to capture system audio (loopback)");
#else
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Note: Only [IN] devices can capture audio on Linux");
#endif
    }
}

static void draw_profiling_window(VisualizerState& vs) {
    if (!vs.show_profiling) return;

    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Profiling", &vs.show_profiling)) {
        ImGui::Text("Audio fetch: %6.2f ms", vs.time_audio_fetch_ms);
        ImGui::Text("FFT:         %6.2f ms", vs.time_fft_ms);
        ImGui::Text("Analysis:    %6.2f ms", vs.time_analysis_ms);
        ImGui::Text("Render:      %6.2f ms", vs.time_render_ms);
        ImGui::Separator();
        ImGui::Text("Total:       %6.2f ms", vs.time_total_ms);
        ImGui::Text("Budget:      %6.2f ms (60fps)", 16.67f);
        float headroom = 16.67f - vs.time_total_ms;
        if (headroom >= 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Headroom:    %6.2f ms", headroom);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Over budget: %6.2f ms", -headroom);
        }
        ImGui::Separator();

        // Stacked area plot of profiling history
        if (ImPlot::BeginPlot("Frame Time", ImVec2(-1, 150))) {
            ImPlot::SetupAxes("Frame", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 25, ImPlotCond_Once);

            // Reorder history so newest is on right (circular buffer unwrap)
            static float plot_audio[VisualizerState::PROFILE_HISTORY_SIZE];
            static float plot_fft[VisualizerState::PROFILE_HISTORY_SIZE];
            static float plot_analysis[VisualizerState::PROFILE_HISTORY_SIZE];
            static float plot_render[VisualizerState::PROFILE_HISTORY_SIZE];
            for (int i = 0; i < vs.PROFILE_HISTORY_SIZE; ++i) {
                int src = (vs.history_idx + i) % vs.PROFILE_HISTORY_SIZE;
                plot_audio[i] = vs.history_audio[src];
                plot_fft[i] = vs.history_fft[src];
                plot_analysis[i] = vs.history_analysis[src];
                plot_render[i] = vs.history_render[src];
            }

            // Stacked shaded areas (bottom to top: audio, fft, analysis, render)
            ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.6f);
            ImPlot::PlotShaded("Audio", plot_audio, vs.PROFILE_HISTORY_SIZE);
            // Stack FFT on top of audio
            static float stack_fft[VisualizerState::PROFILE_HISTORY_SIZE];
            for (int i = 0; i < vs.PROFILE_HISTORY_SIZE; ++i)
                stack_fft[i] = plot_audio[i] + plot_fft[i];
            ImPlot::PlotShaded("FFT", plot_audio, stack_fft, vs.PROFILE_HISTORY_SIZE);
            // Stack analysis on top
            static float stack_analysis[VisualizerState::PROFILE_HISTORY_SIZE];
            for (int i = 0; i < vs.PROFILE_HISTORY_SIZE; ++i)
                stack_analysis[i] = stack_fft[i] + plot_analysis[i];
            ImPlot::PlotShaded("Analysis", stack_fft, stack_analysis, vs.PROFILE_HISTORY_SIZE);
            // Stack render on top
            static float stack_render[VisualizerState::PROFILE_HISTORY_SIZE];
            for (int i = 0; i < vs.PROFILE_HISTORY_SIZE; ++i)
                stack_render[i] = stack_analysis[i] + plot_render[i];
            ImPlot::PlotShaded("Render", stack_analysis, stack_render, vs.PROFILE_HISTORY_SIZE);
            ImPlot::PopStyleVar();

            // 60fps budget line
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            static float budget_line[VisualizerState::PROFILE_HISTORY_SIZE];
            for (int i = 0; i < vs.PROFILE_HISTORY_SIZE; ++i) budget_line[i] = 16.67f;
            ImPlot::PlotLine("60fps Budget", budget_line, vs.PROFILE_HISTORY_SIZE);
            ImPlot::PopStyleColor();

            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Waveform / FFT / Spectrum Analyzer
// ---------------------------------------------------------------------------

static void draw_waveform_and_fft(VisualizerState& vs, AppState* state, int sample_rate) {
    // Settings controls (FFT size, update interval) - shown inline when enabled
    if (vs.show_settings) {
        const char* fft_size_labels[] = { "1024", "2048", "4096", "8192", "16384", "32768" };
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("FFT Size", &vs.fft_size_idx, fft_size_labels, 6);
        ImGui::SameLine();
        float hz_per_bin = static_cast<float>(sample_rate) / vs.fft_sizes[vs.fft_size_idx];
        float window_ms_fft = 1000.0f * vs.fft_sizes[vs.fft_size_idx] / sample_rate;
        ImGui::Text("%.1f Hz/bin, %.0fms", hz_per_bin, window_ms_fft);
        ImGui::SetNextItemWidth(150);
        ImGui::SliderFloat("Update Interval", &vs.update_interval_ms, 0.0f, 500.0f, vs.update_interval_ms == 0.0f ? "Continuous" : "%.0f ms");
    }

    draw_status_line(state, sample_rate);
    draw_vu_meter(vs);
    draw_device_selector(state);

    // Sync settings to FFTView
    vs.fft_view.use_classic_view = vs.use_classic_view;

    // Waveform, FFT, and Spectrum Analyzer via FFTView
    vs.fft_view.draw_waveform();

    if (vs.fft_view.use_classic_view)
        vs.fft_view.draw_classic_fft();
    else
        vs.fft_view.draw_detailed_fft();

    if (vs.show_spectrum_analyzer)
        vs.fft_view.draw_spectrum_analyzer();

    // Sync back (FFTView's detailed_fft has a classic toggle checkbox)
    vs.use_classic_view = vs.fft_view.use_classic_view;
    vs.time_window_ms = vs.fft_view.time_window_ms;
}

// --screenshot: write what is drawn so far (before the ImGui pass) as a PPM, then quit
static void take_screenshot_if_due(AppState* state) {
    static int frame = 0;
    if (state->screenshot_path.empty() || ++frame != state->screenshot_frames) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Magnum::Range2Di rect{{}, {static_cast<int>(display.x), static_cast<int>(display.y)}};
    Magnum::Image2D image = Magnum::GL::defaultFramebuffer.read(rect, {Magnum::PixelFormat::RGBA8Unorm});
    std::FILE* out = std::fopen(state->screenshot_path.c_str(), "wb");
    if (out) {
        const int w = image.size().x();
        const int h = image.size().y();
        std::fprintf(out, "P6\n%d %d\n255\n", w, h);
        const auto pixels = image.pixels<Magnum::Color4ub>();
        std::vector<unsigned char> row(static_cast<std::size_t>(w) * 3);
        for (int y = h - 1; y >= 0; --y) { // GL rows are bottom-up
            for (int x = 0; x < w; ++x) {
                const Magnum::Color4ub c = pixels[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                row[static_cast<std::size_t>(x) * 3 + 0] = c.r();
                row[static_cast<std::size_t>(x) * 3 + 1] = c.g();
                row[static_cast<std::size_t>(x) * 3 + 2] = c.b();
            }
            std::fwrite(row.data(), 1, row.size(), out);
        }
        std::fclose(out);
        std::printf("screenshot: wrote %s (%dx%d)\n", state->screenshot_path.c_str(), w, h);
    } else {
        std::printf("screenshot: cannot open %s\n", state->screenshot_path.c_str());
    }
    state->quit_requested = true;
}

// ---------------------------------------------------------------------------
// Main orchestrator
// ---------------------------------------------------------------------------

static VisualizerState* g_visualizer = nullptr; // the function-static below, for release_gl_resources()

void release_gl_resources() {
    if (g_visualizer) g_visualizer->plasma.release_gl();
}

static void draw_fft_window(AppState* state) {
    static VisualizerState vs;
    g_visualizer = &vs;

    // command-line startup (once): --plasma shows only the plasma, full-screen
    static bool startup_applied = false;
    if (!startup_applied) {
        startup_applied = true;
        if (state->start_plasma) {
            vs.plasma.show = true;
            vs.plasma.render_to_background = true;
            show_visualizer = false;
            show_music_browser = false;
            vs.show_profiling = false;
            vs.show_waterfall_waveform = false;
        }
        if (state->start_waveform) {
            vs.show_waterfall_waveform = true;
            vs.waterfall_waveform_view.render_to_background = true;
            vs.waterfall_waveform_view.waveform_history = true;
            show_visualizer = false;
            show_music_browser = false;
            vs.show_profiling = false;
        }
        if (!state->start_preset.empty()) vs.plasma.load_preset(state->start_preset);
        if (!state->start_display.empty()) vs.plasma.set_display(state->start_display);
    }

    vs.fft->set_use_fftw_direct(vs.use_fftw_direct);

    auto profile_start = std::chrono::high_resolution_clock::now();
    auto section_start = profile_start;

    draw_menu_bar(vs);

    // Create dockspace over viewport
    ImGuiID dockspace_id = ImGui::GetID("VvavvizDockspace");
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    int sample_rate = state->sample_rate;

    // For capture mode, get current sample rate
    if (state->mode == AudioMode::CAPTURE && state->capture && state->capture->is_running()) {
        sample_rate = state->capture->get_sample_rate();
    }

    // Check if FFT buffer size changed
    if (vs.bufsize != vs.fft_sizes[vs.fft_size_idx]) {
        vs.bufsize = vs.fft_sizes[vs.fft_size_idx];
        vs.buffer.set_size(1, vs.bufsize);
        vs.buffer.zeros();
        vs.freqs.set_size(vs.bufsize);
        vs.freqs.zeros();
        vs.mags.set_size(1, vs.bufsize);
        vs.mags.zeros();
    }

    // Update timing
    double current_time = ImGui::GetTime() * 1000.0;
    if (vs.update_interval_ms > 0.0f) {
        if (current_time - vs.last_update_time >= vs.update_interval_ms) {
            vs.should_update = true;
            vs.last_update_time = current_time;
        } else {
            vs.should_update = false;
        }
    } else {
        vs.should_update = true;
    }

    // Audio pipeline: fetch and FFT
    AudioPipeline::fetch_audio(state, vs, sample_rate);

    auto audio_done = std::chrono::high_resolution_clock::now();
    float audio_ms = std::chrono::duration<float, std::milli>(audio_done - section_start).count();
    vs.time_audio_fetch_ms = 0.9f * vs.time_audio_fetch_ms + 0.1f * audio_ms;
    section_start = audio_done;

    AudioPipeline::compute_fft(vs, sample_rate);

    auto fft_done = std::chrono::high_resolution_clock::now();
    float fft_ms = std::chrono::duration<float, std::milli>(fft_done - section_start).count();
    vs.time_fft_ms = 0.9f * vs.time_fft_ms + 0.1f * fft_ms;
    section_start = fft_done;

    // Spectral analysis via SpectralView
    if (vs.fft_enabled && vs.should_update) {
        vs.spectral_view.update(vs.freqs, vs.mags, vs.bufsize, sample_rate, ImGui::GetTime());

        // Extract 5-band peak energy for SH blob (peak = more responsive)
        vibe::daw::extract_5bands_peak(vs.freqs, vs.mags,
                                        vs.sub, vs.kick, vs.mid5, vs.snare, vs.hat);
    }

    // Smooth 5-band values
    {
        float sm = vs.band_smoothing;
        vs.sub_smooth  = sm * vs.sub_smooth  + (1.0f - sm) * vs.sub;
        vs.kick_smooth = sm * vs.kick_smooth + (1.0f - sm) * vs.kick;
        vs.mid5_smooth = sm * vs.mid5_smooth + (1.0f - sm) * vs.mid5;
        // Percussive: fast attack, slow decay
        float snare_sm = (vs.snare > vs.snare_smooth) ? 0.1f : 0.95f;
        vs.snare_smooth = snare_sm * vs.snare_smooth + (1.0f - snare_sm) * vs.snare;
        float hat_sm = (vs.hat > vs.hat_smooth) ? 0.1f : 0.95f;
        vs.hat_smooth = hat_sm * vs.hat_smooth + (1.0f - hat_sm) * vs.hat;

        // Feed SH blob views
        vs.sh_blob_view.set_audio(vs.sub_smooth, vs.kick_smooth, vs.mid5_smooth,
                                   vs.snare_smooth, vs.hat_smooth);
        vs.sh_blob_3d_view.set_audio(vs.sub_smooth, vs.kick_smooth, vs.mid5_smooth,
                                      vs.snare_smooth, vs.hat_smooth);
        vs.sh_blob_3d_view.set_fft_data(vs.freqs, vs.mags);
        vs.sh_blob_3d_view.set_bpm(vs.spectral_view.estimated_bpm());
    }

    // Fluid plasma: raw band peaks -> BandTracker/emitters -> fixed-step sim
    if (vs.plasma.show) {
        PlasmaInput plasma_in;
        // a finished track leaves the last buffer frozen (fetch_audio stops refilling):
        // feed silence instead of re-analyzing it forever
        const bool track_ended = state->mode == AudioMode::FILE_PLAYBACK && state->pb && state->pb->is_done;
        if (!track_ended) {
            plasma_in.raw = {vs.sub, vs.kick, vs.mid5, vs.snare, vs.hat};
            plasma_in.rms_db = vibe::daw::compute_levels(vs.buffer).rms_db;
        }
        plasma_in.chroma = &vs.spectral_view.chromagram();
        plasma_in.bpm = vs.spectral_view.estimated_bpm();
        vs.plasma.update(plasma_in, ImGui::GetIO().DeltaTime);
    }

    auto analysis_done = std::chrono::high_resolution_clock::now();
    float analysis_ms = std::chrono::duration<float, std::milli>(analysis_done - section_start).count();
    vs.time_analysis_ms = 0.9f * vs.time_analysis_ms + 0.1f * analysis_ms;
    section_start = analysis_done;

    // Feed data to FFTView and WaterfallViews
    vs.fft_view.set_fft_data(vs.freqs, vs.mags);
    vs.fft_view.set_waveform(vs.display_buffer, sample_rate);

    // FFT waterfall (OpenGL)
    if (vs.show_waterfall) {
        vs.waterfall_view.set_fft_data(vs.freqs, vs.mags);
    }
    // Waveform waterfall (OpenGL)
    if (vs.show_waterfall_waveform) {
        vs.waterfall_waveform_view.use_waveform = true;
        // Plain-lines mode: one uniform color so zero-crossings aren't dark (ribbons use
        // wave_color -> wave_peak_color, which never goes darker than the base)
        auto& wv = vs.waterfall_waveform_view;
        wv.color_low = wv.color_mid = wv.color_high = wv.wave_color;
        vs.waterfall_waveform_view.set_waveform(vs.display_buffer, sample_rate);
        if (!vs.stereo_buffer.is_empty()) {
            vs.waterfall_waveform_view.set_stereo_waveform(vs.stereo_buffer, sample_rate);
        }
        // Feed audio energy per band for concentric rings
        vs.waterfall_waveform_view.set_audio_energy(
            vs.sub_smooth, vs.kick_smooth, vs.mid5_smooth,
            vs.snare_smooth, vs.hat_smooth);
    }

    // Draw main visualizer window
    if (show_visualizer) {
        if (ImGui::Begin("Audio Visualizer", &show_visualizer)) {
            draw_waveform_and_fft(vs, state, sample_rate);
            vs.spectral_view.draw_spectrogram();
            vs.spectral_view.draw_spectral_flux();
            vs.spectral_view.draw_chromagram();
        }
        ImGui::End();
    }

    // Waterfall windows (OpenGL)
    if (vs.show_waterfall) {
        if (vs.waterfall_view.render_to_background) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            vs.waterfall_view.draw_background(
                static_cast<int>(display.x), static_cast<int>(display.y));
        }
        vs.waterfall_view.draw_window("Waterfall (FFT)", &vs.show_waterfall);
    }
    if (vs.show_waterfall_waveform) {
        if (vs.waterfall_waveform_view.render_to_background) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            vs.waterfall_waveform_view.draw_background(
                static_cast<int>(display.x), static_cast<int>(display.y));
        }
        vs.waterfall_waveform_view.draw_window("Waterfall (Waveform)", &vs.show_waterfall_waveform);
    }


    // Plasma (background renders before the ImGui pass)
    if (vs.plasma.show) {
        if (vs.plasma.render_to_background) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            vs.plasma.draw_background(static_cast<int>(display.x), static_cast<int>(display.y));
        }
        vs.plasma.draw_window("Plasma", &vs.plasma.show);
    }
    take_screenshot_if_due(state);

    // SH Blob windows
    if (vs.show_sh_blob_2d)
        vs.sh_blob_view.draw_window("SH Blob 2D", &vs.show_sh_blob_2d);
    if (vs.show_sh_blob_3d) {
        // Background mode: render blob to default framebuffer before ImGui overlay
        if (vs.sh_blob_3d_view.render_to_background) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            vs.sh_blob_3d_view.draw_background(
                static_cast<int>(display.x), static_cast<int>(display.y));
        }
        vs.sh_blob_3d_view.draw_window("SH Blob 3D", &vs.show_sh_blob_3d);
    }

    // Profile: render done
    auto render_done = std::chrono::high_resolution_clock::now();
    float render_ms = std::chrono::duration<float, std::milli>(render_done - section_start).count();
    vs.time_render_ms = 0.9f * vs.time_render_ms + 0.1f * render_ms;
    float total_ms = std::chrono::duration<float, std::milli>(render_done - profile_start).count();
    vs.time_total_ms = 0.9f * vs.time_total_ms + 0.1f * total_ms;

    // Update history buffers
    vs.history_audio[vs.history_idx] = vs.time_audio_fetch_ms;
    vs.history_fft[vs.history_idx] = vs.time_fft_ms;
    vs.history_analysis[vs.history_idx] = vs.time_analysis_ms;
    vs.history_render[vs.history_idx] = vs.time_render_ms;
    vs.history_total[vs.history_idx] = vs.time_total_ms;
    vs.history_idx = (vs.history_idx + 1) % vs.PROFILE_HISTORY_SIZE;

    draw_profiling_window(vs);
}

void draw_gui(AppState* state) {
    static bool first_frame = true;

    draw_fft_window(state);

    if (show_music_browser) {
        draw_file_browser(state, &show_music_browser);
    }

    // Focus the visualizer on first frame so it's the default active tab
    if (first_frame) {
        ImGui::SetWindowFocus("Audio Visualizer");
        first_frame = false;
    }
}
