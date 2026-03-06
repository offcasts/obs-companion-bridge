cmake_minimum_required(VERSION 3.16)

# ─── obs-plugintemplate bootstrap ────────────────────────────────────────────
# MUST come before project() — this sets up the OBS SDK paths so that
# find_package(libobs) and find_package(obs-frontend-api) work correctly.
include("${CMAKE_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-companion-bridge VERSION 1.0.0)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" OFF)

# ─── Template cmake helpers ───────────────────────────────────────────────────
include(compilerconfig)
include(defaults)
include(helpers)

# ─── nlohmann/json (header-only, auto-downloaded) ────────────────────────────
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Plugin target ────────────────────────────────────────────────────────────
add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/companion-bridge.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  src/
  ${nlohmann_json_SOURCE_DIR}/include
)

# ─── Link OBS libraries ───────────────────────────────────────────────────────
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  nlohmann_json::nlohmann_json
)

# obs-frontend-api only exists in full (non-headless) OBS builds
if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api QUIET)
  if(TARGET OBS::obs-frontend-api)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
  endif()
endif()

# ─── Platform-specific libraries ─────────────────────────────────────────────
if(WIN32)
  # WinSock2 for HTTP server, Windows DNS-SD for mDNS
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 dnsapi)
elseif(APPLE)
  # Bonjour / dns_sd is part of the macOS system — no extra install needed
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    "-framework CoreFoundation"
    "-framework SystemConfiguration"
  )
endif()

# ─── Version definition ───────────────────────────────────────────────────────
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

# ─── Template finalisation ────────────────────────────────────────────────────
# set_target_properties_plugin wires up install paths, code signing, etc.
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
