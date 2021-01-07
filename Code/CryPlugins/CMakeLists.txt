# Mandatory plugin, contains entities required by the engine
if (OPTION_ENGINE)
	add_subdirectory(CryDefaultEntities/Module)
endif()

# GamePlatform: Optional plugin; option PLUGIN_GAMEPLATFORM to enable/disable it resides in its own sub directory
add_subdirectory(CryGamePlatform)

add_subdirectory(CryHTTP/Module)
