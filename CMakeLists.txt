cmake_minimum_required(VERSION 3.20)
project(GameEngine LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This engine uses DirectX 12 / Win32 and builds on Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 外部ライブラリ(imgui / DirectXTex)を置いたフォルダ。ソースの #include "externals/imgui/imgui.h" はこの親フォルダ基準
set(ENGINE_EXTERNALS_DIR "${CMAKE_SOURCE_DIR}/externals" CACHE PATH "Folder that contains imgui/ and DirectXTex/")

add_subdirectory(VirtualisEngine)
add_subdirectory(Game)
