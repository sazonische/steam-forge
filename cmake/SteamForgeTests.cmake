function(steamforge_add_test testName sourceFile)
	set(testTarget "steamforge_${testName}_test")
	qt_add_executable(${testTarget} ${sourceFile})
	target_link_libraries(${testTarget} PRIVATE steamforge_application Qt6::Test)
	target_precompile_headers(${testTarget} PRIVATE "${PROJECT_SOURCE_DIR}/src/pch.hpp" <QtTest>)
	target_compile_definitions(${testTarget} PRIVATE
		STEAMFORGE_SOURCE_DIRECTORY="${PROJECT_SOURCE_DIR}"
	)

	# Tests run headless in CI; the offscreen platform needs no display server.
	qt_import_plugins(${testTarget} INCLUDE Qt6::QOffscreenIntegrationPlugin)

	steamforge_apply_compiler_options(${testTarget})
	steamforge_apply_static_runtime(${testTarget})

	add_test(NAME ${testName} COMMAND ${testTarget})
	set_tests_properties(${testName} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
