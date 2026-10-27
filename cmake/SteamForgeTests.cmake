# Helpers shared by tests; their precompiled header is reused by every test, so Qt headers are compiled once.
function(steamforge_add_test_support)
	add_library(steamforge_test_support STATIC
		tests/support/cloud_computer.h
		tests/support/demo_profiles.h
		tests/support/fake_community_transport.h
		tests/support/fake_drive.h
		tests/support/fake_google_transport.h
		tests/support/fake_key_store.h
		tests/support/fake_web_api_transport.h
		tests/support/local_http_server.h
		tests/support/loopback_browser.h
		tests/support/render_language.cpp
		tests/support/render_language.h
		tests/support/synthetic_tokens.cpp
		tests/support/synthetic_tokens.h
	)
	target_link_libraries(steamforge_test_support PUBLIC steamforge_application Qt6::Test)
	target_include_directories(steamforge_test_support PUBLIC "${PROJECT_SOURCE_DIR}/tests")
	target_compile_definitions(steamforge_test_support PUBLIC
		STEAMFORGE_SOURCE_DIRECTORY="${PROJECT_SOURCE_DIR}"
	)
	target_precompile_headers(steamforge_test_support PRIVATE "${PROJECT_SOURCE_DIR}/src/pch.hpp" <QtTest>)
	steamforge_apply_compiler_options(steamforge_test_support)
endfunction()

function(steamforge_add_test testName sourceFile)
	set(testTarget "steamforge_${testName}_test")
	qt_add_executable(${testTarget} ${sourceFile})
	# Qt gives windows the IDI_ICON1 of the executable, so demonstrations show the icon of SteamForge.
	if(WIN32)
		target_sources(${testTarget} PRIVATE "${PROJECT_BINARY_DIR}/steamforge_test.rc")
	endif()
	target_link_libraries(${testTarget} PRIVATE steamforge_test_support)
	target_precompile_headers(${testTarget} REUSE_FROM steamforge_test_support)

	# Tests run headless in CI; the offscreen platform needs no display server.
	qt_import_plugins(${testTarget} INCLUDE Qt6::QOffscreenIntegrationPlugin)

	steamforge_apply_compiler_options(${testTarget})
	steamforge_apply_static_runtime(${testTarget})

	add_test(NAME ${testName} COMMAND ${testTarget})
	set_tests_properties(${testName} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
