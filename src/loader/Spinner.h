// Spinner.h -- Custom ImDrawList widgets used by the splash loader.

#pragma once

#include <imgui.h>

namespace hked {

// Draws a glowing, rotating arc centered at `center`. `t_seconds` is the
// elapsed application time; the spinner animates on its own.
// `radius` is in pixels. `thickness` is the arc width.
void DrawSpinner(ImDrawList* dl,
                 ImVec2 center,
                 float radius,
                 float thickness,
                 float t_seconds,
                 ImU32 color,
                 ImU32 track_color,
                 ImU32 glow_color);

// Smooth gradient progress bar with rounded caps and a soft sweep highlight.
// `progress` is in [0, 1]; values are clamped.
void DrawProgressBar(ImDrawList* dl,
                     ImVec2 top_left,
                     ImVec2 size,
                     float progress,
                     float t_seconds,
                     ImU32 track_color,
                     ImU32 fill_color_a,
                     ImU32 fill_color_b,
                     ImU32 sheen_color);

// Three-dot pulse indicator, typically rendered next to the subtitle.
void DrawDots(ImDrawList* dl,
              ImVec2 top_left,
              float t_seconds,
              ImU32 color);

} // namespace hked
