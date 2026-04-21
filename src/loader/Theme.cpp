#include "Theme.h"

namespace hked {

static ImU32 RGBA(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

const Palette& DefaultPalette() {
    static const Palette p = [] {
        Palette x{};
        // Deep navy gradient -- close to Tailwind slate-950 -> indigo-950.
        x.bg_top       = RGBA(10,  14,  39);
        x.bg_bottom    = RGBA( 6,   9,  27);
        x.panel        = RGBA(15,  22,  52, 220);
        x.panel_border = RGBA(48,  66, 128,  90);

        x.text_primary = RGBA(226, 232, 255);
        x.text_muted   = RGBA(138, 151, 194);
        x.text_accent  = RGBA(147, 197, 253);

        // Electric blue accent family.
        x.accent       = RGBA( 96, 165, 250);
        x.accent_soft  = RGBA( 96, 165, 250,  70);
        x.accent_glow  = RGBA( 59, 130, 246, 140);
        x.track        = RGBA( 30,  41,  80);

        x.close_idle   = RGBA(138, 151, 194);
        x.close_hover  = RGBA(239, 108, 108);
        return x;
    }();
    return p;
}

void ApplySplashStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding      = 14.0f;
    s.ChildRounding       = 10.0f;
    s.FrameRounding       =  6.0f;
    s.PopupRounding       =  8.0f;
    s.ScrollbarRounding   =  8.0f;
    s.GrabRounding        =  6.0f;
    s.TabRounding         =  6.0f;

    s.WindowBorderSize    = 0.0f;
    s.ChildBorderSize     = 1.0f;
    s.FrameBorderSize     = 0.0f;

    s.WindowPadding       = ImVec2(24, 22);
    s.FramePadding        = ImVec2(10,  6);
    s.ItemSpacing         = ImVec2(10,  8);
    s.ItemInnerSpacing    = ImVec2( 6,  4);

    const Palette& p = DefaultPalette();
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImGui::ColorConvertU32ToFloat4(p.bg_bottom);
    c[ImGuiCol_ChildBg]        = ImGui::ColorConvertU32ToFloat4(p.panel);
    c[ImGuiCol_Border]         = ImGui::ColorConvertU32ToFloat4(p.panel_border);
    c[ImGuiCol_Text]           = ImGui::ColorConvertU32ToFloat4(p.text_primary);
    c[ImGuiCol_TextDisabled]   = ImGui::ColorConvertU32ToFloat4(p.text_muted);
    c[ImGuiCol_FrameBg]        = ImGui::ColorConvertU32ToFloat4(p.track);
    c[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(p.track);
    c[ImGuiCol_FrameBgActive]  = ImGui::ColorConvertU32ToFloat4(p.track);
    c[ImGuiCol_PlotHistogram]  = ImGui::ColorConvertU32ToFloat4(p.accent);
    c[ImGuiCol_ScrollbarBg]    = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]  = ImGui::ColorConvertU32ToFloat4(p.accent_soft);
    c[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(p.accent);
    c[ImGuiCol_ScrollbarGrabActive]  = ImGui::ColorConvertU32ToFloat4(p.accent);
}

} // namespace hked
