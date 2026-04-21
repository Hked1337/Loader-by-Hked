#include "Loader.h"

#include "Spinner.h"
#include "Theme.h"
#include "security/Security.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// Forward-declare the ImGui Win32 message handler from the backend.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace hked {

// ===========================================================================
// LoaderHandle
// ===========================================================================

void LoaderHandle::SetProgress(float value) {
    std::lock_guard<std::mutex> lk(mutex_);
    progress_ = std::clamp(value, 0.0f, 1.0f);
}

void LoaderHandle::SetStage(std::wstring text) {
    std::lock_guard<std::mutex> lk(mutex_);
    stage_ = std::move(text);
}

void LoaderHandle::Log(std::wstring line) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (log_.size() > 256) log_.erase(log_.begin(), log_.begin() + 64);
    log_.emplace_back(std::move(line));
}

bool LoaderHandle::IsCancelled() const noexcept { return cancelled_.load(); }

void LoaderHandle::Finish() { finished_.store(true); }

float LoaderHandle::GetProgress() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return progress_;
}

std::wstring LoaderHandle::GetStage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return stage_;
}

std::vector<std::wstring> LoaderHandle::SnapshotLog() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return log_;
}

bool LoaderHandle::IsFinished() const noexcept { return finished_.load(); }
void LoaderHandle::Cancel()                     { cancelled_.store(true); }

// ===========================================================================
// Platform / DX11 scaffolding
// ===========================================================================

namespace {

struct D3DContext {
    ID3D11Device*           device       = nullptr;
    ID3D11DeviceContext*    context      = nullptr;
    IDXGISwapChain*         swap_chain   = nullptr;
    ID3D11RenderTargetView* rtv          = nullptr;
};

bool CreateDeviceAndSwapChain(HWND hwnd, D3DContext& ctx) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    // flags |= D3D11_CREATE_DEVICE_DEBUG; // enable if you have the SDK layer.
#endif

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION, &sd,
        &ctx.swap_chain, &ctx.device, &obtained, &ctx.context);

    if (FAILED(hr)) {
        // Fall back to WARP if the hardware path fails (unusual but possible in VMs).
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION, &sd,
            &ctx.swap_chain, &ctx.device, &obtained, &ctx.context);
    }

    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    ctx.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    ctx.device->CreateRenderTargetView(back, nullptr, &ctx.rtv);
    back->Release();
    return true;
}

void DestroyD3D(D3DContext& ctx) {
    if (ctx.rtv)        { ctx.rtv->Release();        ctx.rtv        = nullptr; }
    if (ctx.swap_chain) { ctx.swap_chain->Release(); ctx.swap_chain = nullptr; }
    if (ctx.context)    { ctx.context->Release();    ctx.context    = nullptr; }
    if (ctx.device)     { ctx.device->Release();     ctx.device     = nullptr; }
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

constexpr int kHeaderHeight = 42;

struct WindowState {
    HWND          hwnd             = nullptr;
    D3DContext*   d3d              = nullptr;
    LoaderHandle* handle           = nullptr;
    bool          draggable_header = true;
    bool          resize_pending   = false;
    UINT          pending_w        = 0;
    UINT          pending_h        = 0;
};

LRESULT CALLBACK LoaderWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 0;

    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE: {
            if (state && wp != SIZE_MINIMIZED) {
                state->resize_pending = true;
                state->pending_w      = LOWORD(lp);
                state->pending_h      = HIWORD(lp);
            }
            return 0;
        }
        case WM_NCHITTEST: {
            if (state && state->draggable_header) {
                POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ScreenToClient(hwnd, &p);
                RECT rc; GetClientRect(hwnd, &rc);
                // Reserve a 36x36 area on the top-right for the close button.
                if (p.y >= 0 && p.y < kHeaderHeight && p.x < rc.right - 40) {
                    return HTCAPTION;
                }
            }
            break;
        }
        case WM_SYSCOMMAND: {
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0; // disable Alt menu
            break;
        }
        case WM_CLOSE: {
            if (state && state->handle) state->handle->Cancel();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateSplashWindow(HINSTANCE hinst, const LoaderConfig& cfg, WindowState& state) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = LoaderWndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HkedLoaderSplash";
    RegisterClassExW(&wc);

    HMONITOR mon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    GetMonitorInfoW(mon, &mi);
    int mw = mi.rcWork.right  - mi.rcWork.left;
    int mh = mi.rcWork.bottom - mi.rcWork.top;
    int x  = mi.rcWork.left + (mw - cfg.width)  / 2;
    int y  = mi.rcWork.top  + (mh - cfg.height) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        cfg.title.c_str(),
        WS_POPUP | WS_VISIBLE,
        x, y, cfg.width, cfg.height,
        nullptr, nullptr, hinst, nullptr);

    if (!hwnd) return nullptr;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state.hwnd = hwnd;

    // Rounded corners on Windows 11 (no-op on Win10).
    {
        const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        enum { DWMWCP_ROUND = 2 };
        DWORD pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

void RecreateRenderTarget(D3DContext& d3d, UINT w, UINT h) {
    if (d3d.rtv) { d3d.rtv->Release(); d3d.rtv = nullptr; }
    d3d.swap_chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* back = nullptr;
    d3d.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        d3d.device->CreateRenderTargetView(back, nullptr, &d3d.rtv);
        back->Release();
    }
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

struct Fonts {
    ImFont* title   = nullptr;
    ImFont* h2      = nullptr;
    ImFont* body    = nullptr;
    ImFont* small_  = nullptr;
};

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

ImFont* TryLoadFont(ImGuiIO& io, const std::filesystem::path& path, float size) {
    if (!std::filesystem::exists(path)) return nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;
    std::string p = path.string();
    return io.Fonts->AddFontFromFileTTF(p.c_str(), size, &cfg,
                                        io.Fonts->GetGlyphRangesCyrillic());
}

Fonts LoadFonts(ImGuiIO& io) {
    Fonts f;
    auto assets = ExeDir() / "assets" / "fonts";
    f.title  = TryLoadFont(io, assets / "Montserrat-SemiBold.ttf", 22.0f);
    f.h2     = TryLoadFont(io, assets / "Montserrat-SemiBold.ttf", 16.0f);
    f.body   = TryLoadFont(io, assets / "Montserrat-Regular.ttf",  14.0f);
    f.small_ = TryLoadFont(io, assets / "Montserrat-Regular.ttf",  11.0f);

    // Fallback to default if any font failed to load (e.g. missing assets).
    if (!f.title)  f.title  = io.Fonts->AddFontDefault();
    if (!f.h2)     f.h2     = f.title;
    if (!f.body)   f.body   = f.title;
    if (!f.small_) f.small_ = f.body;
    io.FontDefault = f.body;
    return f;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string W2U8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0,
                                 ws.data(), static_cast<int>(ws.size()),
                                 nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(sz), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        ws.data(), static_cast<int>(ws.size()),
                        out.data(), sz, nullptr, nullptr);
    return out;
}

void DrawGradientBackground(ImDrawList* dl, ImVec2 a, ImVec2 b,
                            ImU32 top, ImU32 bottom) {
    dl->AddRectFilledMultiColor(a, b, top, top, bottom, bottom);
}

} // namespace

// ===========================================================================
// RunLoader
// ===========================================================================

bool RunLoader(const LoaderConfig& cfg) {
    // Synchronous anti-tamper pass. On detection this never returns --
    // control diverges into one of the Trap() strategies, which crash the
    // process in a way that looks like a legitimate bug to casual reversers.
    hked::security::RunStartupChecks();

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    LoaderHandle handle;
    D3DContext   d3d;
    WindowState  state;
    state.d3d              = &d3d;
    state.handle           = &handle;
    state.draggable_header = cfg.draggable;

    HWND hwnd = CreateSplashWindow(hinst, cfg, state);
    if (!hwnd) return false;

    if (!CreateDeviceAndSwapChain(hwnd, d3d)) {
        DestroyWindow(hwnd);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplySplashStyle();
    Fonts fonts = LoadFonts(io);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(d3d.device, d3d.context);

    // Spawn the background anti-tamper watchdog now that the UI is up.
    hked::security::StartWatchdog();

    // Kick off the worker.
    std::thread worker([&] {
        if (cfg.on_work) cfg.on_work(handle);
        handle.Finish();
    });

    const Palette& pal          = DefaultPalette();
    const auto     t0           = std::chrono::steady_clock::now();
    float          smoothed     = 0.0f;
    bool           user_closed  = false;

    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (state.resize_pending && d3d.swap_chain) {
            RecreateRenderTarget(d3d, state.pending_w, state.pending_h);
            state.resize_pending = false;
        }

        const float t_seconds = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - t0).count();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- Root fullscreen window (no decorations) ----------------------
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

        ImGui::Begin("##splash_root", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp_min = ImGui::GetWindowPos();
        const ImVec2 wp_max(wp_min.x + vp->WorkSize.x, wp_min.y + vp->WorkSize.y);

        // Background gradient + subtle inner border glow.
        DrawGradientBackground(dl, wp_min, wp_max, pal.bg_top, pal.bg_bottom);
        dl->AddRect(wp_min, wp_max, pal.panel_border, 14.0f, 0, 1.0f);

        // Decorative accent blob (top-left), gives a sense of lighting.
        {
            ImVec2 c(wp_min.x + 90.0f, wp_min.y - 30.0f);
            for (int i = 0; i < 6; ++i) {
                float r = 80.0f + i * 18.0f;
                ImU32 col = IM_COL32(59, 130, 246, 22 - i * 3);
                dl->AddCircleFilled(c, r, col, 64);
            }
        }

        // --- Header --------------------------------------------------------
        ImGui::PushFont(fonts.title);
        ImGui::SetCursorPos(ImVec2(24, 14));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_primary),
                           "%s", W2U8(cfg.title).c_str());
        ImGui::PopFont();

        ImGui::PushFont(fonts.small_);
        ImGui::SetCursorPos(ImVec2(26, 38));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_muted),
                           "%s", W2U8(cfg.subtitle).c_str());
        // Pulsing dots next to the subtitle.
        {
            ImVec2 tl = ImGui::GetItemRectMax();
            DrawDots(dl, ImVec2(tl.x + 6, tl.y - 8), t_seconds, pal.text_accent);
        }
        ImGui::PopFont();

        // Close (X) button -- drawn manually for consistent styling.
        {
            const float  btn_sz = 28.0f;
            const ImVec2 pos(wp_max.x - btn_sz - 10.0f, wp_min.y + 10.0f);
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton("##close", ImVec2(btn_sz, btn_sz));
            const bool hov = ImGui::IsItemHovered();
            const bool act = ImGui::IsItemActive();
            if (ImGui::IsItemClicked()) {
                handle.Cancel();
                user_closed = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            ImU32 bg = hov ? IM_COL32(239, 108, 108, act ? 220 : 160)
                           : IM_COL32(255, 255, 255, 14);
            dl->AddRectFilled(pos, ImVec2(pos.x + btn_sz, pos.y + btn_sz),
                              bg, 6.0f);
            const float pad = 9.0f;
            ImU32 cross = hov ? IM_COL32(255, 255, 255, 230) : pal.close_idle;
            dl->AddLine(ImVec2(pos.x + pad, pos.y + pad),
                        ImVec2(pos.x + btn_sz - pad, pos.y + btn_sz - pad),
                        cross, 1.5f);
            dl->AddLine(ImVec2(pos.x + btn_sz - pad, pos.y + pad),
                        ImVec2(pos.x + pad, pos.y + btn_sz - pad),
                        cross, 1.5f);
        }

        // --- Spinner block -------------------------------------------------
        const ImVec2 spinner_center(wp_min.x + 96.0f,
                                    wp_min.y + vp->WorkSize.y * 0.5f + 18.0f);
        DrawSpinner(dl, spinner_center, 38.0f, 5.0f, t_seconds,
                    pal.accent, pal.track, pal.accent_glow);

        // --- Stage text + progress ----------------------------------------
        const float text_x = wp_min.x + 168.0f;
        const float text_w = vp->WorkSize.x - (text_x - wp_min.x) - 24.0f;

        ImGui::PushFont(fonts.h2);
        ImGui::SetCursorScreenPos(ImVec2(text_x, spinner_center.y - 44.0f));
        ImGui::PushTextWrapPos(text_x + text_w);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_primary),
                           "%s", W2U8(handle.GetStage()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();

        // Smoothed progress animation.
        const float target = handle.GetProgress();
        smoothed += (target - smoothed) * std::min(1.0f, io.DeltaTime * 6.0f);

        const ImVec2 bar_tl(text_x, spinner_center.y - 8.0f);
        const ImVec2 bar_sz(text_w, 10.0f);
        DrawProgressBar(dl, bar_tl, bar_sz, smoothed, t_seconds,
                        pal.track, pal.accent, pal.text_accent, pal.text_primary);

        ImGui::PushFont(fonts.small_);
        ImGui::SetCursorScreenPos(ImVec2(text_x, bar_tl.y + 16.0f));
        char pct[32];
        std::snprintf(pct, sizeof(pct), "%.0f%%", smoothed * 100.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_muted),
                           "%s", pct);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_muted),
                           "%s", W2U8(cfg.version).c_str());
        ImGui::PopFont();

        // --- Log panel -----------------------------------------------------
        const float panel_y = wp_min.y + vp->WorkSize.y - 96.0f;
        const ImVec2 panel_tl(wp_min.x + 20.0f, panel_y);
        const ImVec2 panel_br(wp_max.x - 20.0f, wp_min.y + vp->WorkSize.y - 20.0f);
        dl->AddRectFilled(panel_tl, panel_br, pal.panel, 10.0f);
        dl->AddRect(panel_tl, panel_br, pal.panel_border, 10.0f, 0, 1.0f);

        ImGui::PushFont(fonts.small_);
        ImGui::SetCursorScreenPos(ImVec2(panel_tl.x + 12.0f, panel_tl.y + 10.0f));
        ImGui::BeginChild("##log",
            ImVec2((panel_br.x - panel_tl.x) - 24.0f,
                   (panel_br.y - panel_tl.y) - 20.0f),
            false,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        {
            auto lines = handle.SnapshotLog();
            const size_t show = std::min<size_t>(lines.size(), 6);
            for (size_t i = lines.size() - show; i < lines.size(); ++i) {
                ImGui::TextColored(
                    ImGui::ColorConvertU32ToFloat4(
                        (i + 1 == lines.size()) ? pal.text_accent : pal.text_muted),
                    "%s  %s",
                    (i + 1 == lines.size()) ? ">" : " ",
                    W2U8(lines[i]).c_str());
            }
            if (lines.empty()) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal.text_muted),
                                   "Waiting for the first task\xE2\x80\xA6");
            }
        }
        ImGui::EndChild();
        ImGui::PopFont();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        // --- Present -------------------------------------------------------
        ImGui::Render();
        const float clear[4] = { 0, 0, 0, 0 };
        d3d.context->OMSetRenderTargets(1, &d3d.rtv, nullptr);
        d3d.context->ClearRenderTargetView(d3d.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        d3d.swap_chain->Present(1, 0);

        if (handle.IsFinished()) {
            // Let the UI show 100% briefly before tearing down.
            if (smoothed >= 0.999f) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
    }

    if (worker.joinable()) {
        handle.Cancel();
        worker.join();
    }

    hked::security::StopWatchdog();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyD3D(d3d);

    return !user_closed && handle.IsFinished();
}

} // namespace hked
