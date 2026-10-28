cmake_minimum_required(VERSION 3.25)

project(SteamForge VERSION 0.1.0 LANGUAGES CXX)

# Release builds pass -DSTEAMFORGE_VERSION=<tag version>; otherwise project(VERSION) is used.
if(NOT DEFINED STEAMFORGE_VERSION OR STEAMFORGE_VERSION STREQUAL "")
	set(STEAMFORGE_VERSION "${PROJECT_VERSION}")
endif()
if(NOT STEAMFORGE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
	message(FATAL_ERROR "STEAMFORGE_VERSION must look like MAJOR.MINOR.PATCH, got '${STEAMFORGE_VERSION}'")
endif()
set(STEAMFORGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(STEAMFORGE_VERSION_MINOR "${CMAKE_MATCH_2}")
set(STEAMFORGE_VERSION_PATCH "${CMAKE_MATCH_3}")

# The OAuth client of Google sign-in comes from the environment or -D, never from Git: release builds take it
# from the repository secrets. It stays in the cache, so a later configure without it keeps Google in the build.
# Without it the build works, only Google sync is not offered.
foreach(googleClientVariable STEAMFORGE_GOOGLE_CLIENT_ID STEAMFORGE_GOOGLE_CLIENT_SECRET)
	if(NOT "$ENV{${googleClientVariable}}" STREQUAL "")
		set(${googleClientVariable} "$ENV{${googleClientVariable}}" CACHE STRING "Google OAuth client of SteamForge" FORCE)
	else()
		set(${googleClientVariable} "" CACHE STRING "Google OAuth client of SteamForge")
	endif()
	if(NOT ${googleClientVariable} MATCHES "^[A-Za-z0-9._-]*$")
		message(FATAL_ERROR "${googleClientVariable} may only contain letters, digits, dots, dashes and underscores")
	endif()
endforeach()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STEAMFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings in SteamForge sources as errors" ON)
option(STEAMFORGE_BUILD_TESTS "Build SteamForge tests" ON)

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Svg LinguistTools)
find_package(OpenSSL 3 REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(ZXing CONFIG REQUIRED)
qt_standard_project_setup(REQUIRES 6.11)

include(cmake/SteamForgeCompilerOptions.cmake)
include(cmake/SteamForgeDeveloperTools.cmake)
include(cmake/SteamForgeStaticQt.cmake)
include(cmake/SteamForgeTranslations.cmake)

steamforge_require_static_qt()

set(executableSource src/app/main.cpp)

file(GLOB_RECURSE
	applicationSources
	CONFIGURE_DEPENDS
	src/*.cpp
	src/*.h
	src/*.hpp
)
list(REMOVE_ITEM applicationSources "${PROJECT_SOURCE_DIR}/${executableSource}")

file(GLOB protocolSources CONFIGURE_DEPENDS proto/*.proto)
add_library(steamforge_protocol STATIC ${protocolSources})
protobuf_generate(
	TARGET steamforge_protocol
	LANGUAGE cpp
	IMPORT_DIRS proto
	PROTOC_OUT_DIR "${PROJECT_BINARY_DIR}/generated/proto"
)
target_include_directories(steamforge_protocol PUBLIC "${PROJECT_BINARY_DIR}/generated/proto")
target_link_libraries(steamforge_protocol PUBLIC protobuf::libprotobuf-lite)

qt_add_library(steamforge_application STATIC ${applicationSources})

configure_file(src/common/version.h.in "${PROJECT_BINARY_DIR}/generated/common/version.h" @ONLY)
configure_file(src/common/google_client.h.in "${PROJECT_BINARY_DIR}/generated/common/google_client.h" @ONLY)

target_include_directories(steamforge_application
	PUBLIC src
	PRIVATE "${PROJECT_BINARY_DIR}/generated"
)
target_precompile_headers(steamforge_application PRIVATE src/pch.hpp)

target_link_libraries(steamforge_application PUBLIC
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	Qt6::Network
	Qt6::Svg
)
target_link_libraries(steamforge_application PUBLIC OpenSSL::Crypto)
target_link_libraries(steamforge_application PUBLIC steamforge_protocol ZXing::ZXing)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(Qt6 6.11 REQUIRED COMPONENTS DBus)
	target_link_libraries(steamforge_application PUBLIC Qt6::DBus)
endif()
if(WIN32)
	# crypt32 and ncrypt: the data key under Windows data protection (DPAPI) or Windows Hello.
	target_link_libraries(steamforge_application PRIVATE dwmapi crypt32 ncrypt)
endif()

if(WIN32)
	set(steamforgeIconFiles resources/icons/app.ico resources/icons/tray.ico)
else()
	set(steamforgeIconFiles resources/icons/app.svg resources/icons/tray.svg)
endif()

qt_add_resources(steamforge_application "application_icons"
	PREFIX "/icons"
	BASE resources/icons
	FILES ${steamforgeIconFiles}
)

# Noto Sans 2.015 (SIL Open Font License 1.1, OFL.txt) from github.com/notofonts/latin-greek-cyrillic.
qt_add_resources(steamforge_application "application_fonts"
	PREFIX "/fonts"
	BASE resources/fonts
	FILES
		resources/fonts/NotoSans-Regular.ttf
		resources/fonts/NotoSans-Medium.ttf
		resources/fonts/NotoSans-Bold.ttf
		resources/fonts/OFL.txt
)

qt_add_resources(steamforge_application "application_styles"
	PREFIX "/styles"
	BASE resources/styles
	FILES
		resources/styles/steamforge.qss
		resources/styles/check.svg
		resources/styles/radio-dot.svg
		resources/styles/chevron-down.svg
		resources/styles/eye.svg
		resources/styles/eye-off.svg
		resources/styles/copy.svg
		resources/styles/copied.svg
		resources/styles/gear.svg
		resources/styles/lock.svg
		resources/styles/lock-open.svg
		resources/styles/lock-alert.svg
		resources/styles/cloud.svg
		resources/styles/cloud-check.svg
		resources/styles/cloud-off.svg
		resources/styles/cloud-alert.svg
		resources/styles/more.svg
		resources/styles/clock.svg
		resources/styles/check-circle.svg
		resources/styles/alert-circle.svg
)

qt_add_resources(steamforge_application "application_illustrations"
	PREFIX "/illustrations"
	BASE resources/illustrations
	FILES
		resources/illustrations/welcome.svg
		resources/illustrations/no_confirmations.svg
		resources/illustrations/sign_in_required.svg
		resources/illustrations/guard_setup.svg
		resources/illustrations/all_set.svg
		resources/illustrations/locked.svg
		resources/illustrations/qr_approval.svg
		resources/illustrations/import_files.svg
		resources/illustrations/step_done.svg
		resources/illustrations/step_todo.svg
		resources/illustrations/google_sync.svg
		resources/illustrations/recovery_key.svg
)

set_property(TARGET steamforge_application PROPERTY QT_EXCLUDE_SOURCES_FROM_TRANSLATION "resources/*")
qt_add_translations(TARGETS steamforge_application
	SOURCE_TARGETS steamforge_application
	TS_FILES translations/steamforge_ru.ts
	RESOURCE_PREFIX /translations
)
steamforge_embed_qt_translations(steamforge_application ru)

steamforge_apply_compiler_options(steamforge_application)

qt_add_executable(steamforge ${executableSource})

if(WIN32)
	enable_language(RC)
	set(STEAMFORGE_ICON_DIRECTORY "${PROJECT_SOURCE_DIR}/resources/icons")
	configure_file(resources/windows/steamforge.rc.in "${PROJECT_BINARY_DIR}/steamforge.rc" @ONLY)
	configure_file(resources/windows/steamforge_test.rc.in "${PROJECT_BINARY_DIR}/steamforge_test.rc" @ONLY)
	target_sources(steamforge PRIVATE "${PROJECT_BINARY_DIR}/steamforge.rc")
endif()

target_link_libraries(steamforge PRIVATE steamforge_application)
target_precompile_headers(steamforge REUSE_FROM steamforge_application)

set_target_properties(steamforge PROPERTIES
	OUTPUT_NAME SteamForge
	WIN32_EXECUTABLE ON
)

steamforge_apply_compiler_options(steamforge)
steamforge_apply_static_runtime(steamforge)
steamforge_publish_compile_commands()

if(STEAMFORGE_BUILD_TESTS)
	enable_testing()
	find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
	include(cmake/SteamForgeTests.cmake)
	steamforge_add_test_support()

	file(GLOB_RECURSE
		testSources
		CONFIGURE_DEPENDS
		tests/*_test.cpp
	)
	foreach(testSource IN LISTS testSources)
		get_filename_component(testFileName "${testSource}" NAME_WE)
		string(REGEX REPLACE "_test$" "" testName "${testFileName}")
		steamforge_add_test(${testName} "${testSource}")
	endforeach()
endif()
