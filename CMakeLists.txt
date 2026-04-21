cmake_minimum_required(VERSION 3.20)

project(LoaderByHked
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Dark-blue splash-screen loader built on Dear ImGui + DirectX 11"
)

# ---- Platform guard ---------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR
        "LoaderByHked targets Windows 10/11 only (DirectX 11 + Win32). "
        "Current platform: ${CMAKE_SYSTEM_NAME}.")
endif()

# ---- Global build settings --------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Fetch Dear ImGui (docking branch) --------------------------------------
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC d3d11 dxgi dwmapi)

# ---- Loader library ---------------------------------------------------------
add_library(hked_loader STATIC
    src/loader/Loader.cpp
    src/loader/Theme.cpp
    src/loader/Spinner.cpp
)
target_include_directories(hked_loader PUBLIC src)
target_link_libraries(hked_loader PUBLIC imgui)

# ---- Demo executable --------------------------------------------------------
add_executable(LoaderDemo WIN32
    src/demo/main.cpp
    src/demo/resource.rc
)
target_link_libraries(LoaderDemo PRIVATE hked_loader)
set_target_properties(LoaderDemo PROPERTIES
    VS_DPI_AWARE "PerMonitor"
)

# ---- Copy assets next to the binary -----------------------------------------
add_custom_command(TARGET LoaderDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:LoaderDemo>/assets
    COMMENT "Copying assets (fonts) next to LoaderDemo"
)

# ---- Install rules (optional) ----------------------------------------------
install(TARGETS LoaderDemo RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION bin/assets)
