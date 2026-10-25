function(steamforge_embed_qt_translations target)
	set(qtTranslationDirectory "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
	set(qtTranslationFiles)
	foreach(languageCode IN LISTS ARGN)
		set(qtTranslationFile "${qtTranslationDirectory}/qtbase_${languageCode}.qm")
		if(NOT EXISTS "${qtTranslationFile}")
			message(FATAL_ERROR "Qt translation ${qtTranslationFile} not found. The qttranslations vcpkg port must be installed.")
		endif()
		list(APPEND qtTranslationFiles "${qtTranslationFile}")
	endforeach()

	qt_add_resources(${target} "qt_translations"
		PREFIX "/translations/qt"
		BASE "${qtTranslationDirectory}"
		FILES ${qtTranslationFiles}
	)
endfunction()
