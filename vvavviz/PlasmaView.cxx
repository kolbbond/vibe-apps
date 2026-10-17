// PlasmaView implementation
#include "PlasmaView.hxx"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/RenderbufferFormat.h>
#include <Magnum/GL/Sampler.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/Math/Range.h>

namespace fluid = vibe::fluid;
using Magnum::Color3;
using Magnum::Vector2;
using Magnum::Vector2i;

namespace {

constexpr float kTick = 1.0f / 60.0f;
constexpr float kMaxStirSpeed = 5.0f; // field heights per second
const std::pair<int, int> kResolutions[] = {{128, 72}, {192, 108}, {256, 144}, {320, 180}};

bool common_controls(const char* label, fluid::EmitterCommon& c) {
    ImGui::SeparatorText(label);
    bool changed = ImGui::Checkbox("on", &c.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::SliderFloat("gain", &c.gain, 0.0f, 4.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::SliderFloat("threshold", &c.threshold, 0.0f, 1.0f);
    ImGui::SameLine();
    changed |= ImGui::ColorEdit3("color", c.color.data(), ImGuiColorEditFlags_NoInputs);
    return changed;
}

} // namespace

PlasmaView::PlasmaView() {
    apply_preset(fluid::Preset{});
    refresh_preset_list();
}

void PlasmaView::apply_preset(const fluid::Preset& preset) {
    _preset = preset;
    if(!_solver)
        _solver = fluid::makeCpuStableFluid(preset.width, preset.height);
    else if(_solver->width() != preset.width || _solver->height() != preset.height)
        _solver->resize(preset.width, preset.height);
    _solver->params = preset.fluid;
    _solver->params.wrapX = preset.look.display == fluid::Display::Planet; // the planet wraps around in x
    reset_camera();
    _emitters.setParams(preset.emitters);
    _emitters.setAspect(static_cast<float>(preset.width) / static_cast<float>(preset.height));
}

void PlasmaView::set_display(fluid::Display display) {
    _preset.look.display = display;
    _solver->params.wrapX = display == fluid::Display::Planet;
    reset_camera();
}

// each 3D mode frames its own shape: the planet (radius 1) needs room around it
void PlasmaView::reset_camera() {
    _camera = vibe::gfx::OrbitCamera{};
    if(_preset.look.display == fluid::Display::Planet) {
        _camera.distance = 3.6f;
        _camera.pitch = 0.3f;
    }
}

bool PlasmaView::set_display(const std::string& name) {
    fluid::Display display;
    if(!fluid::parseDisplay(name, display)) {
        _status = "display: flat, terrain or planet";
        return false;
    }
    set_display(display);
    return true;
}

bool PlasmaView::load_preset(const std::string& nameOrPath) {
    const bool isPath = nameOrPath.find('/') != std::string::npos ||
                        (nameOrPath.size() > 5 && nameOrPath.compare(nameOrPath.size() - 5, 5, ".json") == 0);
    const std::string path = isPath ? nameOrPath : std::string{VVAVVIZ_PRESETS_DIR} + "/" + nameOrPath + ".json";
    fluid::Preset preset;
    std::string error;
    if(!fluid::loadPreset(path, preset, error)) {
        _status = error;
        return false;
    }
    apply_preset(preset);
    _status = "loaded " + preset.name;
    return true;
}

void PlasmaView::refresh_preset_list() {
    _presetFiles.clear();
    std::error_code ec;
    for(const auto& entry : std::filesystem::directory_iterator{VVAVVIZ_PRESETS_DIR, ec})
        if(entry.path().extension() == ".json") _presetFiles.push_back(entry.path().stem().string());
    std::sort(_presetFiles.begin(), _presetFiles.end());
}

void PlasmaView::save_preset(const std::string& name) {
    if(name.empty() || name.find('/') != std::string::npos || name.find('.') != std::string::npos) {
        _status = "preset name: letters, digits, - and _ only";
        return;
    }
    fluid::Preset p = _preset;
    p.name = name;
    p.fluid = _solver->params;
    p.emitters = _emitters.params();
    p.width = _solver->width();
    p.height = _solver->height();
    std::string error;
    const std::string path = std::string{VVAVVIZ_PRESETS_DIR} + "/" + name + ".json";
    if(!fluid::savePreset(path, p, error)) {
        _status = error;
        return;
    }
    _preset.name = name;
    _status = "saved " + path;
    refresh_preset_list();
}

void PlasmaView::update(const PlasmaInput& input, float frameSeconds) {
    _bands.update(input.raw, _simTime, frameSeconds, _frame);
    for(std::size_t b = 0; b < _pendingOnsets.size(); ++b) _pendingOnsets[b] = _pendingOnsets[b] || _frame.onsets[b];
    _frame.loudness = std::clamp((input.rms_db + 60.0f) / 60.0f, 0.0f, 1.0f);
    _frame.chroma.fill(0.0f);
    if(input.chroma && input.chroma->size() >= 12)
        for(std::size_t k = 0; k < 12; ++k) _frame.chroma[k] = static_cast<float>((*input.chroma)[k]);
    _frame.bpm = input.bpm;

    const int ticks = _stepper.advance(frameSeconds, _paused ? 0.0 : 1.0);
    for(int t = 0; t < ticks; ++t) {
        fluid::AudioFrame frame = _frame;
        frame.onsets = _pendingOnsets; // a hit between ticks still bursts, once
        _pendingOnsets.fill(false);
        frame.time = _simTime;
        _emitters.apply(frame, kTick, *_solver);
        _solver->step(kTick);
        _simTime += kTick;
    }
    if(!_paused) {
        _spin = std::fmod(_spin + frameSeconds * 0.02f, 1.0f);
        if(_preset.look.autoRotate && _preset.look.display != fluid::Display::Flat) _camera.orbit(frameSeconds * 0.1f, 0.0f);
    }
}

void PlasmaView::render(Magnum::GL::AbstractFramebuffer& target, Vector2i size) {
    if(!_renderer) _renderer.emplace();
    _renderer->update(*_solver);
    const fluid::RenderLook& look = _preset.look;
    const float aspect = static_cast<float>(size.x()) / static_cast<float>(std::max(1, size.y()));
    if(look.display != fluid::Display::Flat) {
        Magnum::GL::Renderer::setClearColor(Magnum::Color4{0.0f, 0.0f, 0.0f, 1.0f});
        target.clear(Magnum::GL::FramebufferClear::Color | Magnum::GL::FramebufferClear::Depth);
    }
    switch(look.display) {
    case fluid::Display::Flat:
        _renderer->draw(look, _emitters.glow(), _emitters.hue(), _view);
        if(_showVelocity) _renderer->drawVelocity(*_solver, 8, _view);
        break;
    case fluid::Display::Terrain:
        _renderer->drawTerrain(look, _emitters.glow(), _emitters.hue(), _camera, aspect);
        break;
    case fluid::Display::Planet:
        _renderer->drawPlanet(look, _emitters.glow(), _emitters.hue(), _camera, aspect, _spin);
        break;
    }
}

void PlasmaView::ensure_target(Vector2i size) {
    if(size == _fboSize) return;
    _fboSize = size;
    _color = Magnum::GL::Texture2D{};
    _color.setStorage(1, Magnum::GL::TextureFormat::RGBA8, size)
        .setMinificationFilter(Magnum::GL::SamplerFilter::Linear)
        .setMagnificationFilter(Magnum::GL::SamplerFilter::Linear)
        .setWrapping(Magnum::GL::SamplerWrapping::ClampToEdge);
    _fbo = Magnum::GL::Framebuffer{{{}, size}};
    _fbo.attachTexture(Magnum::GL::Framebuffer::ColorAttachment{0}, _color, 0);
    _depth = Magnum::GL::Renderbuffer{};
    _depth.setStorage(Magnum::GL::RenderbufferFormat::DepthComponent24, size);
    _fbo.attachRenderbuffer(Magnum::GL::Framebuffer::BufferAttachment::Depth, _depth);

    // 8x MSAA (the 3D terrain/planet edges need it); resolved into _color each frame
    const int samples = std::min(8, Magnum::GL::Renderbuffer::maxSamples());
    _msaaColor = Magnum::GL::Renderbuffer{};
    _msaaColor.setStorageMultisample(samples, Magnum::GL::RenderbufferFormat::RGBA8, size);
    _msaaDepth = Magnum::GL::Renderbuffer{};
    _msaaDepth.setStorageMultisample(samples, Magnum::GL::RenderbufferFormat::DepthComponent24, size);
    _msaaFbo = Magnum::GL::Framebuffer{{{}, size}};
    _msaaFbo.attachRenderbuffer(Magnum::GL::Framebuffer::ColorAttachment{0}, _msaaColor)
        .attachRenderbuffer(Magnum::GL::Framebuffer::BufferAttachment::Depth, _msaaDepth);
}

void PlasmaView::mouse_input(ImVec2 mouse, ImVec2 rectMin, ImVec2 rectSize, float dt) {
    if(rectSize.x <= 0.0f || rectSize.y <= 0.0f) return;
    const ImGuiIO& io = ImGui::GetIO();
    const Vector2 screen{(mouse.x - rectMin.x) / rectSize.x, 1.0f - (mouse.y - rectMin.y) / rectSize.y};
    if(screen.x() < 0.0f || screen.x() > 1.0f || screen.y() < 0.0f || screen.y() > 1.0f) return;
    if(_preset.look.display != fluid::Display::Flat) {
        if(io.MouseWheel != 0.0f) _camera.zoom(std::pow(1.15f, io.MouseWheel));
        if(io.MouseDown[0]) _camera.orbit(-io.MouseDelta.x * 0.008f, io.MouseDelta.y * 0.008f);
        if(io.MouseDown[1]) _solver->addDye(screen, _stirColor * 0.6f, 0.02f); // no 3D picking: the flat field's point
        return;
    }
    if(io.MouseWheel != 0.0f) _view.zoomAt(screen, std::pow(1.15f, io.MouseWheel));
    if(io.MouseDown[2]) _view.pan(Vector2{io.MouseDelta.x / rectSize.x, -io.MouseDelta.y / rectSize.y});
    stir(screen, rectSize, dt);
}

void PlasmaView::stir(Vector2 screen, ImVec2 rectSize, float dt) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool left = io.MouseDown[0];
    const bool right = io.MouseDown[1];
    if(!left && !right) return;
    const Vector2 at = _view.toField(screen);
    if(at.x() < 0.0f || at.x() > 1.0f || at.y() < 0.0f || at.y() > 1.0f) return; // zoomed out, off the field
    if(left) {
        // screen heights -> field heights: zoomed in, the same drag covers less of the field
        Vector2 velocity =
            Vector2{io.MouseDelta.x, -io.MouseDelta.y} / rectSize.y / _view.zoom / std::max(dt, 1.0f / 240.0f) * _stirStrength;
        const float speed = velocity.length();
        if(speed > kMaxStirSpeed) velocity *= kMaxStirSpeed / speed; // alt-tab / first-frame jumps
        _solver->addForce(at, velocity, 0.03f / _view.zoom);
    }
    _solver->addDye(at, _stirColor * 0.6f, 0.02f / _view.zoom);
}

void PlasmaView::draw_background(int width, int height) {
    Magnum::GL::defaultFramebuffer.bind();
    Magnum::GL::defaultFramebuffer.setViewport({{}, {width, height}});
    render(Magnum::GL::defaultFramebuffer, {width, height});
    const ImGuiIO& io = ImGui::GetIO();
    if(!io.WantCaptureMouse) mouse_input(io.MousePos, ImVec2{0.0f, 0.0f}, io.DisplaySize, io.DeltaTime);
}

void PlasmaView::draw_window(const char* title, bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(560, 700), ImGuiCond_FirstUseEver);
    if(ImGui::Begin(title, p_open)) {
        if(!render_to_background) {
            const float w = std::max(64.0f, ImGui::GetContentRegionAvail().x);
            const float h = std::max(36.0f, w * static_cast<float>(_preset.height) / static_cast<float>(_preset.width));
            // render at framebuffer pixels, not ImGui points, so HiDPI stays sharp
            const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
            const Vector2i size{static_cast<int>(w * fbScale.x), static_cast<int>(h * fbScale.y)};
            ensure_target(size);
            _msaaFbo.bind();
            render(_msaaFbo, size);
            Magnum::GL::AbstractFramebuffer::blit(_msaaFbo, _fbo, {{}, size}, Magnum::GL::FramebufferBlit::Color);
            Magnum::GL::defaultFramebuffer.bind();
            // an invisible button owns the rect so the wheel zooms instead of scrolling and drags don't move the window
            ImGui::InvisibleButton("plasma", ImVec2{w, h},
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectSize = ImGui::GetItemRectSize();
            ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(_color.id()), rectMin,
                ImVec2{rectMin.x + rectSize.x, rectMin.y + rectSize.y}, ImVec2{0, 1}, ImVec2{1, 0});
            if(ImGui::IsItemHovered() || ImGui::IsItemActive())
                mouse_input(ImGui::GetIO().MousePos, rectMin, rectSize, ImGui::GetIO().DeltaTime);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Rendering to background (drag on it to stir)");
        }
        draw_panel();
    }
    ImGui::End();
}

void PlasmaView::draw_panel() {
    ImGui::Checkbox("Background", &render_to_background);
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &_paused);
    ImGui::SameLine();
    if(ImGui::Button("Reset")) _solver->clear();
    ImGui::SameLine();
    ImGui::Checkbox("Velocity", &_showVelocity);

    if(ImGui::BeginCombo("Preset", _preset.name.c_str())) {
        for(const std::string& name : _presetFiles)
            if(ImGui::Selectable(name.c_str(), name == _preset.name)) load_preset(name);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if(ImGui::SmallButton("Rescan")) refresh_preset_list();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##savename", _saveName, sizeof _saveName);
    ImGui::SameLine();
    if(ImGui::Button("Save preset")) save_preset(_saveName);
    if(!_status.empty()) ImGui::TextDisabled("%s", _status.c_str());
    ImGui::TextDisabled("bands  sub %.2f  kick %.2f  mid %.2f  snare %.2f  hat %.2f", _frame.bands[0], _frame.bands[1],
        _frame.bands[2], _frame.bands[3], _frame.bands[4]);

    if(ImGui::CollapsingHeader("Fluid", ImGuiTreeNodeFlags_DefaultOpen)) {
        fluid::FluidParams& p = _solver->params;
        p.wrapX = _preset.look.display == fluid::Display::Planet; // follows the display mode, not a slider
        ImGui::SliderFloat("Viscosity", &p.viscosity, 0.0f, 2.0f);
        ImGui::SliderFloat("Dye fade", &p.dyeFade, 0.9f, 1.0f, "%.4f");
        ImGui::SliderFloat("Velocity fade", &p.velocityFade, 0.9f, 1.0f, "%.4f");
        ImGui::SliderFloat("Swirl", &p.vorticity, 0.0f, 60.0f);
        ImGui::SliderInt("Pressure iterations", &p.pressureIterations, 0, 60);
        _preset.fluid = p;
        const std::string current = std::to_string(_solver->width()) + "x" + std::to_string(_solver->height());
        if(ImGui::BeginCombo("Resolution", current.c_str())) {
            for(const auto& [w, h] : kResolutions) {
                const std::string label = std::to_string(w) + "x" + std::to_string(h);
                if(ImGui::Selectable(label.c_str(), label == current)) {
                    _preset.width = w;
                    _preset.height = h;
                    _solver->resize(w, h);
                    _emitters.setAspect(static_cast<float>(w) / static_cast<float>(h));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Stir strength", &_stirStrength, 0.0f, 3.0f);
        ImGui::ColorEdit3("Stir color", _stirColor.data(), ImGuiColorEditFlags_NoInputs);
    }

    if(ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen)) {
        fluid::RenderLook& look = _preset.look;
        if(ImGui::RadioButton("Dye colors", look.colorMode == fluid::ColorMode::Dye)) look.colorMode = fluid::ColorMode::Dye;
        ImGui::SameLine();
        if(ImGui::RadioButton("Palette", look.colorMode == fluid::ColorMode::Palette)) look.colorMode = fluid::ColorMode::Palette;
        ImGui::SliderFloat("Glow", &look.glow, 0.0f, 3.0f);
        ImGui::SliderInt("Glow blur", &look.glowLod, 0, 6);
        ImGui::SliderFloat("Exposure", &look.exposure, 0.1f, 6.0f);
        ImGui::SliderFloat("Brightness", &look.brightness, 0.0f, 3.0f);
        if(ImGui::BeginCombo("Display", fluid::displayName(look.display))) {
            for(fluid::Display d : {fluid::Display::Flat, fluid::Display::Terrain, fluid::Display::Planet})
                if(ImGui::Selectable(fluid::displayName(d), d == look.display)) set_display(d);
            ImGui::EndCombo();
        }
        if(look.display == fluid::Display::Flat) {
            ImGui::SetNextItemWidth(180);
            ImGui::SliderFloat("Zoom", &_view.zoom, fluid::FluidView::kMinZoom, fluid::FluidView::kMaxZoom, "%.2fx",
                ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            if(ImGui::Button("Reset view")) _view.reset();
            ImGui::TextDisabled("wheel: zoom at cursor   middle-drag: pan");
        } else {
            ImGui::SliderFloat("Height", &look.height, 0.0f, 1.0f);
            ImGui::Checkbox("Auto-rotate", &look.autoRotate);
            ImGui::SameLine();
            if(ImGui::Button("Reset camera")) reset_camera();
            ImGui::TextDisabled("drag: orbit   wheel: zoom   right-drag: dye");
        }
        ImGui::ColorEdit3("Palette low", look.palette[0].data(), ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::ColorEdit3("mid", look.palette[1].data(), ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::ColorEdit3("high", look.palette[2].data(), ImGuiColorEditFlags_NoInputs);
        ImGui::Text("music glow x%.2f   hue %.2f", _emitters.glow(), _emitters.hue());
    }

    if(ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        fluid::EmitterParams e = _emitters.params();
        ImGui::Checkbox("Music", &e.mix.music);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("music strength", &e.mix.musicStrength, 0.0f, 5.0f);
        ImGui::Checkbox("Ambient", &e.ambient.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("ambient strength", &e.ambient.strength, 0.0f, 5.0f);
        ImGui::PushID("ambient");
        ImGui::SliderFloat("wander speed", &e.ambient.speed, 0.0f, 1.0f);
        ImGui::SliderInt("stirrers", &e.ambient.points, 0, 16);
        ImGui::SliderFloat("stir radius", &e.ambient.radius, 0.02f, 0.3f);
        ImGui::SliderFloat("trail", &e.ambient.dye, 0.0f, 1.0f);
        ImGui::SameLine();
        ImGui::ColorEdit3("color", e.ambient.color.data(), ImGuiColorEditFlags_NoInputs);
        ImGui::PopID();
        _emitters.setParams(e);
        _preset.emitters = e;
    }

    if(ImGui::CollapsingHeader("Emitters")) {
        fluid::EmitterParams e = _emitters.params();
        ImGui::PushID("burst");
        common_controls("Burst (kick hits)", e.burst);
        ImGui::SliderFloat("strength", &e.burst.strength, 0.0f, 5.0f);
        ImGui::SliderFloat("ring radius", &e.burst.ringRadius, 0.0f, 0.4f);
        ImGui::SliderInt("splats", &e.burst.splats, 3, 32);
        ImGui::PopID();
        ImGui::PushID("vortex");
        common_controls("Vortex (sub level)", e.vortex);
        ImGui::SliderFloat("strength", &e.vortex.strength, 0.0f, 5.0f);
        ImGui::SliderFloat("radius", &e.vortex.radius, 0.05f, 0.45f);
        ImGui::PopID();
        ImGui::PushID("jets");
        common_controls("Jets (mid level)", e.jets);
        ImGui::SliderFloat("strength", &e.jets.strength, 0.0f, 5.0f);
        ImGui::SliderFloat("radius", &e.jets.radius, 0.05f, 0.45f);
        ImGui::SliderFloat("rate (no tempo)", &e.jets.rate, 0.0f, 2.0f);
        ImGui::PopID();
        ImGui::PushID("splash");
        common_controls("Splash (snare hits)", e.splash);
        ImGui::SliderFloat("strength", &e.splash.strength, 0.0f, 5.0f);
        ImGui::PopID();
        ImGui::PushID("sparks");
        common_controls("Sparks (hat level)", e.sparks);
        ImGui::SliderInt("count", &e.sparks.count, 0, 80);
        ImGui::PopID();
        ImGui::SeparatorText("Glow / Hue");
        ImGui::SliderFloat("glow base", &e.glow.base, 0.0f, 2.0f);
        ImGui::SliderFloat("glow from loudness", &e.glow.gain, 0.0f, 2.0f);
        ImGui::Checkbox("hue follows the key", &e.hue.enabled);
        ImGui::SliderFloat("hue rate", &e.hue.rate, 0.0f, 2.0f);
        _emitters.setParams(e);
        _preset.emitters = e;
    }
}
