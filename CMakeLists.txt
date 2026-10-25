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
qt_standard_project_setup(REQUIRES 6.11)

include(cmake/SteamForgeCompilerOptions.cmake)
include(cmake/SteamForgeDeveloperTools.cmake)
include(cmake/SteamForgeStaticQt.cmake)
include(cmake/SteamForgeTranslations.cmake)

steamforge_require_static_qt()

qt_add_library(steamforge_application STATIC
	src/pch.hpp
	src/app/application.cpp
	src/app/application.h
	src/app/interface_translator.cpp
	src/app/interface_translator.h
	src/app/single_instance_guard.cpp
	src/app/single_instance_guard.h
	src/common/constants.h
	src/platform/application_icons.cpp
	src/platform/application_icons.h
	src/ui/main_window.cpp
	src/ui/main_window.h
	src/ui/system_tray.cpp
	src/ui/system_tray.h
)

configure_file(src/common/version.h.in "${PROJECT_BINARY_DIR}/generated/common/version.h" @ONLY)

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

qt_add_translations(TARGETS steamforge_application
	SOURCE_TARGETS steamforge_application
	TS_FILES translations/steamforge_ru.ts
	RESOURCE_PREFIX /translations
)
steamforge_embed_qt_translations(steamforge_application ru)

steamforge_apply_compiler_options(steamforge_application)

qt_add_executable(steamforge
	src/app/main.cpp
)

if(WIN32)
	enable_language(RC)
	set(STEAMFORGE_ICON_DIRECTORY "${PROJECT_SOURCE_DIR}/resources/icons")
	configure_file(resources/windows/steamforge.rc.in "${PROJECT_BINARY_DIR}/steamforge.rc" @ONLY)
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

	steamforge_add_test(application_icons tests/platform/application_icons_test.cpp)
	steamforge_add_test(interface_translator tests/app/interface_translator_test.cpp)
endif()
