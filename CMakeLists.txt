cmake_minimum_required(VERSION 3.20)
project(KB VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Algumas dependências ainda declaram cmake_minimum_required antigo.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
# Runtime estático: o KB roda sem instalar o "Visual C++ Redistributable".
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(NOT WIN32)
  message(FATAL_ERROR "O KB para PC é feito para Windows (SAPI, WinHTTP, bandeja do sistema). "
                      "No celular, use o app web servido pelo próprio KB (Configurações > Celular).")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- GLFW
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---------------------------------------------------------------- Dear ImGui (docking + multi-viewport)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.6-docking.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _sem_cmake)

# ---------------------------------------------------------------- miniaudio (captura do microfone)
FetchContent_Declare(miniaudio
  URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _sem_cmake)

# ---------------------------------------------------------------- JSON
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---------------------------------------------------------------- whisper.cpp (voz -> texto, local)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
# Binário portátil (qualquer CPU x64 com AVX2), não só a máquina que compilou.
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_AVX2   ON  CACHE BOOL "" FORCE)
set(GGML_FMA    ON  CACHE BOOL "" FORCE)
set(GGML_F16C   ON  CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper
  URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.7.6.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(glfw json whisper imgui miniaudio)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw opengl32)

set(KB_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
set(KB_OPTS /utf-8 /W3 /permissive-)

# ---------------------------------------------------------------- KB (o assistente)
add_executable(KB WIN32
  src/main.cpp
  src/app.cpp
  src/core/config.cpp
  src/core/platform.cpp
  src/core/text.cpp
  src/core/files.cpp
  src/core/install.cpp
  src/net/http.cpp
  src/net/mobile_server.cpp
  src/net/tunnel.cpp
  src/llm/llm_client.cpp
  src/llm/model_scan.cpp
  src/audio/audio_capture.cpp
  src/audio/stt_whisper.cpp
  src/audio/tts_sapi.cpp
  src/media/image.cpp
  src/media/screen.cpp
  src/ui/theme.cpp
  src/ui/anim.cpp
  src/ui/widgets.cpp
  src/ui/orb.cpp
  src/ui/embeds.cpp
  src/ui/blocks.cpp
  src/ui/views.cpp
  src/ui/assets.cpp
  src/ui/timer.cpp
  src/core/perf.cpp
  third_party/qrcodegen.cpp
  res/kb.rc)
target_include_directories(KB PRIVATE src third_party ${miniaudio_SOURCE_DIR})
target_compile_definitions(KB PRIVATE ${KB_DEFS})
target_compile_options(KB PRIVATE ${KB_OPTS})
set_source_files_properties(res/kb.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/res/mobile.html;${CMAKE_SOURCE_DIR}/res/kb.ico;${CMAKE_SOURCE_DIR}/res/bg.jpg;${CMAKE_SOURCE_DIR}/res/logo.png")
target_link_libraries(KB PRIVATE
  imgui whisper nlohmann_json::nlohmann_json
  winhttp sapi ole32 oleaut32 dwmapi shell32 winmm advapi32 user32 gdi32 comdlg32
  d3d11 dxgi ws2_32 iphlpapi bcrypt uuid pdh)
set_target_properties(KB PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------- KB-Setup (instalador com o KB.exe embutido)
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/setup_payload.rc
     CONTENT "GLFW_ICON ICON \"${CMAKE_SOURCE_DIR}/res/kb.ico\"\nKB_EXE RCDATA \"$<TARGET_FILE:KB>\"\nLOGO_PNG RCDATA \"${CMAKE_SOURCE_DIR}/res/logo.png\"\n")
add_executable(KBSetup WIN32
  src/setup/setup_main.cpp
  src/core/install.cpp
  src/core/platform.cpp
  src/core/text.cpp
  src/core/config.cpp
  src/net/http.cpp
  src/ui/theme.cpp
  src/ui/anim.cpp
  src/ui/widgets.cpp
  src/ui/orb.cpp
  src/ui/assets.cpp
  src/media/image.cpp
  ${CMAKE_BINARY_DIR}/setup_payload.rc)
add_dependencies(KBSetup KB)
set_source_files_properties(${CMAKE_BINARY_DIR}/setup_payload.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/bin/KB.exe")
target_include_directories(KBSetup PRIVATE src third_party)
target_compile_definitions(KBSetup PRIVATE ${KB_DEFS})
target_compile_options(KBSetup PRIVATE ${KB_OPTS})
target_link_libraries(KBSetup PRIVATE imgui nlohmann_json::nlohmann_json winhttp ole32 shell32 dwmapi advapi32 user32 gdi32 comdlg32 uuid)
set_target_properties(KBSetup PROPERTIES OUTPUT_NAME "KB-Setup" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
