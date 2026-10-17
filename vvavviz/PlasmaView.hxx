// PlasmaView - music-driven fluid plasma (vibe-fluid) for vvavviz
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <Magnum/GL/Framebuffer.h>
#include <Magnum/GL/Renderbuffer.h>
#include <Magnum/GL/Texture.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Vector2.h>

#include "vibe/fluid/BandTracker.hxx"
#include "vibe/fluid/Emitters.hxx"
#include "vibe/fluid/FluidSolver.hxx"
#include "vibe/fluid/Preset.hxx"
#include "vibe/fluid/View.hxx"
#include "vibe/game/FixedStepper.hxx"
#include "vibe/graphics/FluidRenderer.hxx"
#include "vibe/graphics/OrbitCamera.hxx"

// what vvavviz's analysis gives the plasma each frame
struct PlasmaInput {
    std::array<float, vibe::fluid::kBands> raw{}; // sub, kick, mid, snare, hat band peaks (unnormalized)
    float rms_db = -60.0f;
    const std::vector<double>* chroma = nullptr;  // 12 pitch classes, or empty
    float bpm = 0.0f;
};

class PlasmaView {
public:
    // free GL objects while the context still exists (the view outlives the window)
    void release_gl() {
        _renderer.reset();
        _fbo = Magnum::GL::Framebuffer{Magnum::NoCreate}; // the windowed-mode render target
        _color = Magnum::GL::Texture2D{Magnum::NoCreate};
        _depth = Magnum::GL::Renderbuffer{Magnum::NoCreate};
        _msaaFbo = Magnum::GL::Framebuffer{Magnum::NoCreate};
        _msaaColor = Magnum::GL::Renderbuffer{Magnum::NoCreate};
        _msaaDepth = Magnum::GL::Renderbuffer{Magnum::NoCreate};
        _fboSize = {};
    }
    PlasmaView();

    bool show = false;
    bool render_to_background = false;

    // audio -> bands/onsets -> emitters -> fixed 60 Hz sim steps
    void update(const PlasmaInput& input, float frameSeconds);
    // full-screen, before the ImGui pass (mouse stirs when ImGui doesn't want it)
    void draw_background(int width, int height);
    // the plasma image (window mode) plus the tuning panel
    void draw_window(const char* title, bool* p_open);

    // name ("lava") or path; false keeps the current look and sets the status line
    bool load_preset(const std::string& nameOrPath);
    void apply_preset(const vibe::fluid::Preset& preset);
    // "flat", "terrain" or "planet"; false keeps the current mode and sets the status line
    bool set_display(const std::string& name);

private:
    void render(Magnum::GL::AbstractFramebuffer& target, Magnum::Vector2i size);
    void set_display(vibe::fluid::Display display);
    void reset_camera();
    void ensure_target(Magnum::Vector2i size);
    // mouse over the plasma: wheel zooms around the cursor, middle-drag pans, left/right stir
    void mouse_input(ImVec2 mouse, ImVec2 rectMin, ImVec2 rectSize, float dt);
    void stir(Magnum::Vector2 screen, ImVec2 rectSize, float dt);
    void draw_panel();
    void refresh_preset_list();
    void save_preset(const std::string& name);

    vibe::fluid::Preset _preset;
    std::unique_ptr<vibe::fluid::FluidSolver> _solver;
    vibe::fluid::EmitterSet _emitters;
    vibe::fluid::BandTracker _bands;
    vibe::fluid::AudioFrame _frame;
    std::array<bool, vibe::fluid::kBands> _pendingOnsets{}; // survive frames that run zero sim ticks
    vibe::game::FixedStepper _stepper{60.0};
    double _simTime = 0.0;
    bool _paused = false;
    bool _showVelocity = false;
    float _stirStrength = 0.6f;
    Magnum::Color3 _stirColor{0.9f, 0.4f, 1.0f};
    vibe::fluid::FluidView _view; // a viewing setting, not part of presets
    vibe::gfx::OrbitCamera _camera; // 3D modes; also not saved
    float _spin = 0.0f;              // planet rotation, turns

    std::optional<vibe::gfx::FluidRenderer> _renderer; // created on first draw (needs GL)
    Magnum::GL::Texture2D _color{Magnum::NoCreate};
    Magnum::GL::Framebuffer _fbo{Magnum::NoCreate};
    Magnum::GL::Renderbuffer _depth{Magnum::NoCreate};
    Magnum::GL::Framebuffer _msaaFbo{Magnum::NoCreate}; // windowed mode renders here, resolved into _fbo
    Magnum::GL::Renderbuffer _msaaColor{Magnum::NoCreate};
    Magnum::GL::Renderbuffer _msaaDepth{Magnum::NoCreate};
    Magnum::Vector2i _fboSize{};

    std::vector<std::string> _presetFiles;
    std::string _status;
    char _saveName[64] = "my-preset";
};
