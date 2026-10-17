// File browser implementation
#include "FileBrowser.hxx"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

const std::vector<std::string> AUDIO_EXTENSIONS = {
    ".wav", ".mp3", ".flac", ".ogg", ".aiff", ".aif", ".m4a", ".wma"
};

bool is_audio_file(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(AUDIO_EXTENSIONS.begin(), AUDIO_EXTENSIONS.end(), ext) != AUDIO_EXTENSIONS.end();
}

void scan_audio_files(AppState* state) {
    state->found_files.clear();
    state->selected_file_idx = -1;

    try {
        if (fs::exists(state->search_path) && fs::is_directory(state->search_path)) {
            for (const auto& entry : fs::recursive_directory_iterator(
                     state->search_path,
                     fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && is_audio_file(entry.path())) {
                    state->found_files.push_back(entry.path().string());
                }
            }
            std::sort(state->found_files.begin(), state->found_files.end());
        }
    } catch (const std::exception& e) {
        std::printf("Error scanning directory: %s\n", e.what());
    }

    state->needs_refresh = false;
}

void draw_file_browser(AppState* state, bool* p_open) {
    if (!ImGui::Begin("Music Browser", p_open)) {
        ImGui::End();
        return;
    }

    // Path input
    static char path_buf[512] = "";
    if (state->search_path.empty()) {
        // Try Windows paths first, then Unix
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
#ifdef _WIN32
            state->search_path = std::string(home) + "\\Music";
#else
            state->search_path = std::string(home) + "/Music";
#endif
            std::strncpy(path_buf, state->search_path.c_str(), sizeof(path_buf) - 1);
        }
    }

    ImGui::Text("Search Path:");
    ImGui::SetNextItemWidth(400);
    if (ImGui::InputText("##path", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        state->search_path = path_buf;
        state->needs_refresh = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        state->search_path = path_buf;
        state->needs_refresh = true;
    }

    // Scan for audio files if needed
    if (state->needs_refresh && !state->search_path.empty()) {
        scan_audio_files(state);
    }

    // Display file count
    ImGui::Text("Found %zu audio files", state->found_files.size());

    // Current file
    if (!state->current_file.empty()) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Playing: %s",
            fs::path(state->current_file).filename().string().c_str());
    }

    ImGui::Separator();

    // File list
    if (ImGui::BeginChild("FileList", ImVec2(0, 300), true)) {
        for (size_t i = 0; i < state->found_files.size(); i++) {
            const auto& filepath = state->found_files[i];
            fs::path p(filepath);
            std::string display_name = p.filename().string();

            bool is_selected = (static_cast<int>(i) == state->selected_file_idx);
            if (ImGui::Selectable(display_name.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                state->selected_file_idx = static_cast<int>(i);

                if (ImGui::IsMouseDoubleClicked(0)) {
                    state->pending_file = filepath;
                    state->file_load_requested = true;
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", filepath.c_str());
            }
        }
    }
    ImGui::EndChild();

    // Load button
    if (state->selected_file_idx >= 0 &&
        state->selected_file_idx < static_cast<int>(state->found_files.size())) {
        if (ImGui::Button("Load Selected")) {
            state->pending_file = state->found_files[state->selected_file_idx];
            state->file_load_requested = true;
        }
    }

    // Switch to capture mode button
    if (state->mode == AudioMode::FILE_PLAYBACK) {
        ImGui::SameLine();
        if (ImGui::Button("Switch to Capture")) {
            if (state->pb) {
                state->pb->is_done = true;
                soundio_wakeup(state->lsio->get_soundio());
            }
            if (state->capture) {
                state->capture->start(state->current_input_idx);
                state->mode = AudioMode::CAPTURE;
                state->sample_rate = state->capture->get_sample_rate();
            }
        }
    }

    ImGui::End();
}
