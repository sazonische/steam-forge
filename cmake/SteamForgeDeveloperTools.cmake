option(STEAMFORGE_PUBLISH_COMPILE_COMMANDS "Copy compile_commands.json to build/ for clangd and clang-tidy" ON)

function(steamforge_publish_compile_commands)
	if(NOT CMAKE_EXPORT_COMPILE_COMMANDS OR NOT STEAMFORGE_PUBLISH_COMPILE_COMMANDS)
		return()
	endif()

	# clangd looks for build/compile_commands.json next to the sources, whichever preset produced it.
	add_custom_target(steamforge_compile_commands ALL
		COMMAND "${CMAKE_COMMAND}"
			"-DSOURCE=${CMAKE_BINARY_DIR}/compile_commands.json"
			"-DDESTINATION=${CMAKE_SOURCE_DIR}/build/compile_commands.json"
			-P "${CMAKE_SOURCE_DIR}/cmake/scripts/PublishCompileCommands.cmake"
		VERBATIM
	)
endfunction()
