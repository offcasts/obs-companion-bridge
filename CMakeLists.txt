cmake_minimum_required(VERSION 3.16)

# ─── obs-plugintemplate bootstrap ────────────────────────────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-companion-bridge VERSION 1.0.0)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" OFF)

# ─── Template cmake helpers ───────────────────────────────────────────────────
include(compilerconfig)
include(defaults)
include(helpers)

# ─── Find OBS packages ────────────────────────────────────────────────────────
find_package(libobs REQUIRED)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api QUIET)
endif()

# ─── nlohmann/json (header-only, auto-downloaded) ────────────────────────────
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Linux: Avahi dns_sd compatibility (optional — graceful fallback) ────────
if(UNIX AND NOT APPLE)
  find_library(AVAHI_COMPAT_LIB avahi-compat-libdns_sd
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
  find_path(AVAHI_COMPAT_INCLUDE_DIR dns_sd.h
    HINTS /usr/include/avahi-compat-libdns_sd)
  if(AVAHI_COMPAT_LIB AND AVAHI_COMPAT_INCLUDE_DIR)
    message(STATUS "Found Avahi dns_sd compat: ${AVAHI_COMPAT_LIB}")
    set(HAVE_AVAHI_COMPAT TRUE)
  else()
    message(STATUS "Avahi dns_sd compat not found — mDNS disabled on Linux")
    set(HAVE_AVAHI_COMPAT FALSE)
  endif()
endif()

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

# ─── Core OBS link ────────────────────────────────────────────────────────────
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  nlohmann_json::nlohmann_json
)

if(ENABLE_FRONTEND_API AND TARGET OBS::obs-frontend-api)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

# ─── Platform-specific libraries ─────────────────────────────────────────────
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 dnsapi)
elseif(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    "-framework CoreFoundation"
    "-framework SystemConfiguration"
  )
elseif(UNIX)
  if(HAVE_AVAHI_COMPAT)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
      ${AVAHI_COMPAT_INCLUDE_DIR})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
      ${AVAHI_COMPAT_LIB})
  endif()
endif()

# ─── Version ─────────────────────────────────────────────────────────────────
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

# ─── Template finalisation ────────────────────────────────────────────────────
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
