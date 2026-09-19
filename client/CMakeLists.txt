cmake_minimum_required(VERSION 3.20)
project(Loader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED API_BASE_URL)
  set(API_BASE_URL "http://localhost:3000")
endif()

if(NOT DEFINED LOADER_BRAND)
  set(LOADER_BRAND "Loader")
endif()

add_compile_definitions(
  LOADER_API_BASE_URL="${API_BASE_URL}"
  LOADER_BRAND="${LOADER_BRAND}"
)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_executable(Loader WIN32
  src/main.cpp
  src/app_window.cpp
  src/http_client.cpp
  src/auth_service.cpp
  src/ui/auth_screen.cpp
  src/ui/custom_widgets.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(Loader PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

target_link_libraries(Loader PRIVATE
  nlohmann_json::nlohmann_json
  d3d11
  dxgi
  winhttp
)

if(MSVC)
  target_compile_options(Loader PRIVATE /utf-8)
endif()
