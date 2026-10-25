function(steamforge_apply_compiler_options target)
	target_compile_definitions(${target} PRIVATE
		QT_NO_KEYWORDS
		QT_NO_CAST_FROM_ASCII
		QT_NO_CAST_TO_ASCII
		QT_NO_URL_CAST_FROM_STRING
	)

	if(WIN32)
		target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
	endif()

	if(MSVC)
		target_compile_options(${target} PRIVATE
			/W4
			/permissive-
			/utf-8
			/EHsc
			/Zc:__cplusplus
			/Zc:preprocessor
			/external:anglebrackets
			/external:W0
		)
		if(STEAMFORGE_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE
			-Wall
			-Wextra
			-Wpedantic
			-Wshadow
			-Wnon-virtual-dtor
			-Wold-style-cast
			-Wcast-align
			-Woverloaded-virtual
			-Wnull-dereference
			-Wdouble-promotion
			-Wformat=2
			-Wimplicit-fallthrough
		)
		if(STEAMFORGE_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()
