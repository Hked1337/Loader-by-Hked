// Demo entry point for LoaderByHked. Shows the splash, simulates a few
// startup stages, and exits. Drop this logic into your real app and replace
// the fake work with whatever your initialization does.

#include "loader/Loader.h"

#include <array>
#include <chrono>
#include <string>
#include <thread>

namespace {

struct Stage {
    const wchar_t* label;
    int            steps;      // how many sub-steps this stage takes
    int            millis;     // ms per step
};

constexpr std::array<Stage, 6> kStages = { {
    { L"Checking environment",     12,  40 },
    { L"Loading configuration",    18,  35 },
    { L"Connecting to services",   22,  45 },
    { L"Downloading assets",       40,  35 },
    { L"Warming up caches",        20,  30 },
    { L"Finalizing",                8,  50 },
} };

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    hked::LoaderConfig cfg;
    cfg.title    = L"Loader by Hked";
    cfg.subtitle = L"Preparing your workspace";
    cfg.version  = L"v1.0.0";

    cfg.on_work = [](hked::LoaderHandle& h) {
        int total_steps = 0;
        for (const auto& s : kStages) total_steps += s.steps;

        int done = 0;
        for (const auto& s : kStages) {
            if (h.IsCancelled()) return;
            h.SetStage(s.label);
            h.Log(std::wstring(L"\u25B8 ") + s.label);
            for (int i = 0; i < s.steps; ++i) {
                if (h.IsCancelled()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(s.millis));
                ++done;
                h.SetProgress(static_cast<float>(done) /
                              static_cast<float>(total_steps));
            }
        }
        h.SetStage(L"All set. Launching\u2026");
        h.Log(L"\u2713 Startup complete");
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    };

    const bool ok = hked::RunLoader(cfg);
    return ok ? 0 : 1;
}
