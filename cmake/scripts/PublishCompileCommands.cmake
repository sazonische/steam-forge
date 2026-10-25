# Usage: cmake -DSOURCE=<compile_commands.json> -DDESTINATION=<copy> -P PublishCompileCommands.cmake
# The copy is for clangd and clang-tidy: they cannot read MSVC precompiled headers, so the
# PCH use flags are dropped and the forced include of the PCH header is parsed instead.

file(READ "${SOURCE}" compileCommands)

string(REGEX REPLACE " /Y[cu][^ \"]+" "" compileCommands "${compileCommands}")
string(REGEX REPLACE " /Fp[^ \"]+" "" compileCommands "${compileCommands}")
string(REPLACE " -Winvalid-pch" "" compileCommands "${compileCommands}")
string(REPLACE " /WX " " /WX -Wno-unused-command-line-argument " compileCommands "${compileCommands}")

if(EXISTS "${DESTINATION}")
	file(READ "${DESTINATION}" publishedCompileCommands)
	if(publishedCompileCommands STREQUAL compileCommands)
		return()
	endif()
endif()

file(WRITE "${DESTINATION}.tmp" "${compileCommands}")
file(RENAME "${DESTINATION}.tmp" "${DESTINATION}")
