// Theme.h -- Dark-blue ImGui palette used by the splash loader.

#pragma once

#include <imgui.h>

namespace hked {

struct Palette {
    // Window / surfaces
    ImU32 bg_top;       // top gradient
    ImU32 bg_bottom;    // bottom gradient
    ImU32 panel;        // log panel background
    ImU32 panel_border; // log panel border

    // Text
    ImU32 text_primary;
    ImU32 text_muted;
    ImU32 text_accent;

    // Accent / progress
    ImU32 accent;       // primary accent (electric blue)
    ImU32 accent_soft;  // translucent halo
    ImU32 accent_glow;  // outer glow for spinner
    ImU32 track;        // progress / ring track

    // Controls
    ImU32 close_idle;
    ImU32 close_hover;
};

const Palette& DefaultPalette();

// Applies ImGui style tweaks (rounding, paddings, colors) for the splash.
void ApplySplashStyle();

} // namespace hked
