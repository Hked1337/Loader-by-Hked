# Loader by Hked

A polished dark-blue splash-screen loader for Windows desktop apps, built on
[Dear ImGui](https://github.com/ocornut/imgui) + DirectX 11 and the
[Montserrat](https://fonts.google.com/specimen/Montserrat) type family.

![palette](https://img.shields.io/badge/theme-dark--blue-1e3a8a)
![platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0ea5e9)
![cxx](https://img.shields.io/badge/C%2B%2B-17-3b82f6)

## What you get

- Borderless, draggable splash window, centered on the primary monitor.
- Rounded corners on Windows 11 (via DWM), square on Windows 10 - same code.
- Dear ImGui + DirectX 11 renderer, feature-level fallback to 10.1 / 10.0 and
  WARP, so it runs on virtually any Win10/Win11 install.
- Custom animated spinner, gradient progress bar with a moving sheen, three
  pulsing status dots, and a scrolling stage log panel.
- A cohesive dark-blue palette (see [`src/loader/Theme.cpp`](src/loader/Theme.cpp)).
- Montserrat (Regular / SemiBold / Bold) shipped under
  [`assets/fonts/`](assets/fonts/), SIL OFL 1.1.
- A tiny reusable C++ API you call from your own `WinMain`:

  ```cpp
  hked::LoaderConfig cfg;
  cfg.title    = L"Acme Client";
  cfg.subtitle = L"Preparing your workspace";
  cfg.version  = L"v1.2.3";
  cfg.on_work  = [](hked::LoaderHandle& h) {
      for (int i = 0; i <= 100 && !h.IsCancelled(); ++i) {
          h.SetStage(L"Loading module " + std::to_wstring(i));
          h.SetProgress(i / 100.0f);
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
  };
  hked::RunLoader(cfg);
  ```

## Build (Windows 10 / 11, x64)

Prerequisites:

- Visual Studio 2019 or 2022 with the **Desktop development with C++** workload
  (MSVC v142 / v143, Windows 10 SDK 10.0.19041+ is fine).
- CMake 3.20+.
- Internet access on the first configure (CMake `FetchContent` pulls
  Dear ImGui `v1.90.9-docking`).

```powershell
git clone https://github.com/Hked1337/Loader-by-Hked.git
cd Loader-by-Hked
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\bin\Release\LoaderDemo.exe
```

The build copies `assets/` next to the executable, so `LoaderDemo.exe` finds
the Montserrat fonts out of the box. If the fonts are missing at runtime, the
loader silently falls back to Dear ImGui's default font.

## Project layout

```
.
|-- CMakeLists.txt          # MSVC x64, C++17, fetches ImGui docking branch
|-- assets/
|   `-- fonts/              # Montserrat (Regular/SemiBold/Bold) + OFL.txt
|-- src/
|   |-- loader/             # Reusable static library `hked_loader`
|   |   |-- Loader.h        # Public API: RunLoader(), LoaderConfig, LoaderHandle
|   |   |-- Loader.cpp      # Win32 window + DX11 + ImGui main loop
|   |   |-- Theme.h/.cpp    # Dark-blue palette + ImGui style
|   |   `-- Spinner.h/.cpp  # Animated spinner / progress bar / dots
|   `-- demo/
|       |-- main.cpp        # Fake startup pipeline driving the loader
|       `-- resource.rc     # Placeholder for your .ico / VERSIONINFO
`-- README.md
```

## Integrating into your own app

1. Copy the `src/loader/` folder and `assets/fonts/` into your project.
2. Link `d3d11.lib`, `dxgi.lib`, `dwmapi.lib` (already handled for you if you
   reuse this repository's `CMakeLists.txt`).
3. Call `hked::RunLoader(cfg)` from your `WinMain` before showing your real
   main window. The call blocks until the worker lambda finishes or the user
   closes the splash.

The worker lambda runs on its own thread - never touch ImGui or the HWND from
it. Communicate only via `LoaderHandle::SetStage / SetProgress / Log`.

## Customizing

- **Colors**: edit the palette in
  [`src/loader/Theme.cpp`](src/loader/Theme.cpp). Every color in the splash
  routes through `hked::DefaultPalette()`.
- **Fonts**: swap the TTF files in `assets/fonts/` or change the sizes inside
  `LoadFonts()` in [`src/loader/Loader.cpp`](src/loader/Loader.cpp).
- **Window size**: tweak `LoaderConfig::width` / `height`. The layout is laid
  out in pixels relative to the window, so 560x360 is the tested default.

## Licenses

- Source code in this repo: MIT-style (feel free to reuse; attribution
  appreciated but not required).
- Montserrat: SIL Open Font License 1.1 - see
  [`assets/fonts/OFL.txt`](assets/fonts/OFL.txt).
- Dear ImGui: MIT, fetched from upstream at configure time.
