# VirtualisEngine = ゲームに依存しない静的ライブラリ。別のゲームからもこのフォルダごと使える
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(VirtualisEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(VirtualisEngine PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/.."   # #include "VirtualisEngine/Core/Logger.h"
  "${ENGINE_EXTERNALS_DIR}/.."       # #include "externals/imgui/imgui.h"
  "${ENGINE_EXTERNALS_DIR}/DirectXTex" # #include "DirectXTex.h"
)
target_compile_definitions(VirtualisEngine PUBLIC USE_IMGUI)   # ImGuiのUIを有効にする(外すとUIコードが消える)
if(MSVC)
  target_compile_options(VirtualisEngine PUBLIC /utf-8 /W4 /permissive-)
endif()

# ---- 外部ライブラリ ----
# imgui: ソースがあればここで静的ライブラリとして作る(プロジェクト側で作っている場合は不要)
set(IMGUI_DIR "${ENGINE_EXTERNALS_DIR}/imgui")
if(EXISTS "${IMGUI_DIR}/imgui.cpp" AND NOT TARGET imgui)
  add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp" "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp" "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_impl_dx12.cpp" "${IMGUI_DIR}/imgui_impl_win32.cpp")
  target_include_directories(imgui PUBLIC "${IMGUI_DIR}")
endif()
if(TARGET imgui)
  target_link_libraries(VirtualisEngine PUBLIC imgui)
endif()

# DirectXTex: CMakeLists.txt があればここで追加する(無ければ DirectXTex というターゲットを自分で用意する)
if(EXISTS "${ENGINE_EXTERNALS_DIR}/DirectXTex/CMakeLists.txt" AND NOT TARGET DirectXTex)
  add_subdirectory("${ENGINE_EXTERNALS_DIR}/DirectXTex" "${CMAKE_BINARY_DIR}/DirectXTex")
endif()
if(TARGET DirectXTex)
  target_link_libraries(VirtualisEngine PUBLIC DirectXTex)
endif()

# Windowsのライブラリは各ソースの #pragma comment(lib, ...) でリンクされる
