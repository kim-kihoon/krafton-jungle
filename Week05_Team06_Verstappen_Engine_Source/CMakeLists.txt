cmake_minimum_required(VERSION 3.15)
project(cpp-gametechlab-project-w05)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "include/*.h"
)

set(IMGUI_SOURCES
    "ImGui/imgui.cpp"
    "ImGui/imgui_demo.cpp"
    "ImGui/imgui_draw.cpp"
    "ImGui/imgui_tables.cpp"
    "ImGui/imgui_widgets.cpp"
    "ImGui/imgui_impl_dx11.cpp"
    "ImGui/imgui_impl_win32.cpp"
)

add_executable(VerstappenEngine WIN32 ${SOURCES} ${IMGUI_SOURCES} "include/Graphics/DebugLine.h" "src/Graphics/DebugLine.cpp" "src/Core/PlatformTime.cpp")

target_include_directories(VerstappenEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGui"
)

if(MSVC)
    target_compile_definitions(VerstappenEngine PRIVATE
        UNICODE
        _UNICODE
        WITH_IMGUI=1
        VERSTAPPEN_WITH_IMGUI=1
    )
    target_compile_options(VerstappenEngine PRIVATE
        /MP
        /W4
        /utf-8
        /permissive-
    )

    target_compile_options(VerstappenEngine PRIVATE
        $<$<CONFIG:Release>:
            /Ox
            /Ob2
            /Oi
            /Ot
            /GL
            /fp:fast
            /arch:AVX2
            /GF
            /Gy
            /Zc:inline
        >
    )

    set_target_properties(VerstappenEngine PROPERTIES
        LINK_FLAGS_RELEASE "/LTCG"
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

target_link_libraries(VerstappenEngine PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    windowscodecs.lib
)
