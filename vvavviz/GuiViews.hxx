// GUI view functions for vvavviz
#pragma once

#include "AppState.hxx"

// Initialize ImGui docking (call once on first frame)
void init_docking();

// Draw all GUI windows (visualizer, file browser, profiling)
void draw_gui(AppState* state);

// free GL resources held by function-static views; call before the window/GL context is destroyed
void release_gl_resources();
