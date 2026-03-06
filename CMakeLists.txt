cmake_minimum_required(VERSION 3.16)
project(obs-companion-bridge VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── OBS Plugin boilerplate ───────────────────────────────────────────────────
# Uses obs-plugintemplate conventions.
# The template automatically downloads the OBS SDK via buildspec.json.

find_package(libobs REQUIRED)

# ─── nlohmann/json (header-only, fetched automatically) ──────────────────────
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Plugin target ────────────────────────────────────────────────────────────
add_library(obs-companion-bridge MODULE
  src/plugin-main.cpp
  src/companion-bridge.cpp
)

target_include_directories(obs-companion-bridge PRIVATE
  src/
  ${nlohmann_json_SOURCE_DIR}/include
)

# ─── Core libs (all platforms) ───────────────────────────────────────────────
target_link_libraries(obs-companion-bridge PRIVATE
  OBS::libobs
  nlohmann_json::nlohmann_json
)

# ─── obs-frontend-api (only available in full OBS builds) ────────────────────
# The macOS CI builds OBS in headless mode, so this target may not exist there.
if(TARGET OBS::obs-frontend-api)
  target_link_libraries(obs-companion-bridge PRIVATE OBS::obs-frontend-api)
endif()

# ─── Windows-only socket and DNS libs ────────────────────────────────────────
# ws2_32 = WinSock2 (HTTP server), dnsapi = Windows DNS-SD (mDNS advertiser)
# These don't exist on macOS/Linux — the platform headers handle their own APIs.
if(WIN32)
  target_link_libraries(obs-companion-bridge PRIVATE
    ws2_32
    dnsapi
  )
endif()

# ─── macOS-only frameworks ───────────────────────────────────────────────────
# dns_sd is part of the system on macOS — no extra install needed.
if(APPLE)
  target_link_libraries(obs-companion-bridge PRIVATE
    "-framework CoreFoundation"
    "-framework SystemConfiguration"
  )
endif()

# ─── Compiler definitions ────────────────────────────────────────────────────
target_compile_definitions(obs-companion-bridge PRIVATE
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

# ─── MSVC-specific flags ──────────────────────────────────────────────────────
if(MSVC)
  target_compile_options(obs-companion-bridge PRIVATE
    /W3
    /WX-
    /MP
    /utf-8
  )
  target_compile_definitions(obs-companion-bridge PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()

# ─── Output name ─────────────────────────────────────────────────────────────
set_target_properties(obs-companion-bridge PROPERTIES
  PREFIX ""
)

# ─── Install paths ────────────────────────────────────────────────────────────
if(WIN32)
  install(TARGETS obs-companion-bridge
    LIBRARY DESTINATION "obs-plugins/64bit"
    RUNTIME DESTINATION "obs-plugins/64bit"
  )
  install(DIRECTORY data/
    DESTINATION "data/obs-plugins/obs-companion-bridge"
    OPTIONAL
  )
elseif(APPLE)
  install(TARGETS obs-companion-bridge
    LIBRARY DESTINATION "obs-plugins"
  )
  install(DIRECTORY data/
    DESTINATION "data/obs-plugins/obs-companion-bridge"
    OPTIONAL
  )
endif()
