#include "Spinner.h"

#include <algorithm>
#include <cmath>

namespace hked {

namespace {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 2.0f * kPi;

ImU32 Lerp(ImU32 a, ImU32 b, float t) {
    ImVec4 va = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 vb = ImGui::ColorConvertU32ToFloat4(b);
    ImVec4 vc(va.x + (vb.x - va.x) * t,
              va.y + (vb.y - va.y) * t,
              va.z + (vb.z - va.z) * t,
              va.w + (vb.w - va.w) * t);
    return ImGui::ColorConvertFloat4ToU32(vc);
}

ImU32 WithAlpha(ImU32 c, float alpha) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(v);
}

} // namespace

void DrawSpinner(ImDrawList* dl,
                 ImVec2 center,
                 float radius,
                 float thickness,
                 float t_seconds,
                 ImU32 color,
                 ImU32 track_color,
                 ImU32 glow_color) {
    // Outer soft glow -- rendered as a few concentric faded rings.
    for (int i = 4; i >= 1; --i) {
        float r = radius + thickness * 0.5f + i * 3.0f;
        ImU32 g = WithAlpha(glow_color, 0.10f + 0.06f * (4 - i));
        dl->AddCircle(center, r, g, 64, thickness * 0.6f);
    }

    // Full faded track.
    dl->AddCircle(center, radius, track_color, 96, thickness);

    // Rotating arc with gradient along its length.
    const float sweep   = kPi * 1.25f;              // 225 degrees
    const float rot     = t_seconds * 2.2f;         // rev speed
    const int   segs    = 64;
    const float a0      = rot;
    const float a1      = rot + sweep;

    ImVec2 prev;
    bool   has_prev = false;
    for (int i = 0; i <= segs; ++i) {
        float k     = static_cast<float>(i) / segs;
        float a     = a0 + (a1 - a0) * k;
        ImVec2 p    = ImVec2(center.x + std::cos(a) * radius,
                             center.y + std::sin(a) * radius);
        if (has_prev) {
            ImU32 c = Lerp(WithAlpha(color, 0.05f), color, k);
            dl->AddLine(prev, p, c, thickness);
        }
        prev     = p;
        has_prev = true;
    }

    // Leading head dot -- a filled circle at the bright end of the arc.
    ImVec2 head(center.x + std::cos(a1) * radius,
                center.y + std::sin(a1) * radius);
    dl->AddCircleFilled(head, thickness * 0.65f, color, 20);

    // Pulsing core dot in the middle.
    float pulse = 0.5f + 0.5f * std::sin(t_seconds * 2.6f);
    float coreR = radius * (0.16f + 0.05f * pulse);
    dl->AddCircleFilled(center, coreR, WithAlpha(color, 0.55f + 0.35f * pulse), 32);
}

void DrawProgressBar(ImDrawList* dl,
                     ImVec2 top_left,
                     ImVec2 size,
                     float progress,
                     float t_seconds,
                     ImU32 track_color,
                     ImU32 fill_color_a,
                     ImU32 fill_color_b,
                     ImU32 sheen_color) {
    progress = std::clamp(progress, 0.0f, 1.0f);

    const ImVec2 br(top_left.x + size.x, top_left.y + size.y);
    const float  r = size.y * 0.5f;

    // Track.
    dl->AddRectFilled(top_left, br, track_color, r);

    if (progress <= 0.0001f) return;

    // Fill (horizontal gradient).
    const float fill_w = size.x * progress;
    const ImVec2 fbr(top_left.x + fill_w, br.y);

    dl->AddRectFilledMultiColor(
        top_left,
        fbr,
        fill_color_a, fill_color_b, fill_color_b, fill_color_a);

    // Re-apply rounding mask by drawing rounded rect outline overlay.
    dl->AddRectFilled(top_left, fbr, IM_COL32(0, 0, 0, 0), r);

    // Moving sheen highlight (diagonal sweep inside the fill area).
    const float sheen_w = 36.0f;
    float       sx      = std::fmod(t_seconds * 180.0f, fill_w + sheen_w * 2.0f) - sheen_w;
    if (sx > 0.0f && sx < fill_w) {
        float x0 = top_left.x + std::max(0.0f, sx - sheen_w * 0.5f);
        float x1 = top_left.x + std::min(fill_w, sx + sheen_w * 0.5f);
        dl->AddRectFilledMultiColor(
            ImVec2(x0, top_left.y), ImVec2(x1, br.y),
            WithAlpha(sheen_color, 0.00f),
            WithAlpha(sheen_color, 0.35f),
            WithAlpha(sheen_color, 0.35f),
            WithAlpha(sheen_color, 0.00f));
    }
}

void DrawDots(ImDrawList* dl, ImVec2 top_left, float t_seconds, ImU32 color) {
    const float r      = 2.5f;
    const float gap    = 10.0f;
    for (int i = 0; i < 3; ++i) {
        float phase = t_seconds * 3.0f - i * 0.45f;
        float a     = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(phase));
        ImVec2 c(top_left.x + r + i * gap, top_left.y + r);
        dl->AddCircleFilled(c, r, WithAlpha(color, a), 16);
    }
}

} // namespace hked
