// Loader.h -- Dark-blue splash-screen loader built on Dear ImGui + DirectX 11.
//
// Usage (minimal):
//
//     hked::LoaderConfig cfg;
//     cfg.title      = L"Acme Client";
//     cfg.subtitle   = L"Preparing your workspace";
//     cfg.version    = L"v1.2.3";
//     cfg.on_work    = [](hked::LoaderHandle& h) {
//         for (int i = 0; i <= 100 && !h.IsCancelled(); ++i) {
//             h.SetProgress(i / 100.0f);
//             h.SetStage(L"Loading module " + std::to_wstring(i));
//             std::this_thread::sleep_for(std::chrono::milliseconds(25));
//         }
//     };
//     hked::RunLoader(cfg);
//
// The call is blocking: it creates a borderless DX11 window, spawns the
// worker thread, pumps the UI, and returns once the worker completes (or the
// user closes the splash). Designed for Windows 10 / Windows 11, x64.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace hked {

// Handle exposed to the background worker. Thread-safe.
class LoaderHandle {
public:
    // Report overall progress in [0, 1]. Values are clamped. Smoothed visually.
    void SetProgress(float value);

    // Update the headline stage text shown under the spinner.
    void SetStage(std::wstring text);

    // Append a line to the scrolling log pane.
    void Log(std::wstring line);

    // True once the user clicked the close button. Worker should bail out
    // cleanly when this flips to true.
    bool IsCancelled() const noexcept;

    // Request the splash window to close (called from the worker when done).
    void Finish();

    // --- internal ----------------------------------------------------------
    float        GetProgress() const;
    std::wstring GetStage() const;
    std::vector<std::wstring> SnapshotLog() const;
    bool         IsFinished() const noexcept;
    void         Cancel();

private:
    mutable std::mutex        mutex_;
    float                     progress_  = 0.0f;
    std::wstring              stage_     = L"Starting\u2026";
    std::vector<std::wstring> log_;
    std::atomic<bool>         cancelled_ { false };
    std::atomic<bool>         finished_  { false };
};

struct LoaderConfig {
    // Branding
    std::wstring title     = L"Loader by Hked";
    std::wstring subtitle  = L"Initializing\u2026";
    std::wstring version   = L"v1.0.0";

    // Window
    int          width     = 560;
    int          height    = 360;
    bool         draggable = true;   // Drag via the top header area.

    // Worker -- runs on its own thread. Must not touch the UI directly;
    // communicate through the LoaderHandle.
    std::function<void(LoaderHandle&)> on_work;
};

// Runs the splash screen. Returns true if the worker finished on its own,
// false if the user cancelled (closed the window).
bool RunLoader(const LoaderConfig& cfg);

} // namespace hked
