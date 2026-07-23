cmake_minimum_required(VERSION 3.16)
project(lastfm-discord-presence)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ── Discord Social SDK ────────────────────────────────────────────────────────
set(DISCORD_SDK_ROOT "${CMAKE_SOURCE_DIR}/lib/discord_social_sdk")
set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/release")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/release")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(lastfm-discord-presence
  src/main.cpp
  src/lastfm.cpp
  src/store.cpp
)

target_include_directories(lastfm-discord-presence PRIVATE
  ${DISCORD_SDK_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/lib
)

# Find libcurl (system, brew, or manual -DCURL_* paths)
find_package(CURL REQUIRED)
if(CURL_FOUND)
  target_include_directories(lastfm-discord-presence PRIVATE ${CURL_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})
  target_link_libraries(lastfm-discord-presence PRIVATE ${CURL_LIBRARIES})
endif()

# Platform-specific Discord SDK library
if(WIN32)
  set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
  set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")
elseif(APPLE)
  set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
  set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
else()
  set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
  set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
endif()

target_link_libraries(lastfm-discord-presence PRIVATE ${DISCORD_LIB_PATH})

# RPATH so the shared lib is found at runtime
if(APPLE)
  set_target_properties(lastfm-discord-presence PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path"
  )
elseif(UNIX)
  set_target_properties(lastfm-discord-presence PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
  )
endif()

# Copy shared lib next to the binary
add_custom_command(TARGET lastfm-discord-presence POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${DISCORD_SHARED_LIB}"
  $<TARGET_FILE_DIR:lastfm-discord-presence>
)
