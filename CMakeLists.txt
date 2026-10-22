cmake_minimum_required(VERSION 3.28)
project(SonicSunset)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine is IluminationEngine (https://github.com/Lagamy/IlluminationEngine, README.md there), a separate repo checked out next to this one and linked here by the symlink IluminationEngine -> ../IluminationEngine.
# Its options are set before add_subdirectory. FORCE makes these lines win over the cache, so a change here is picked up by the next CMake run with no -D flag; a -D on the command line is ignored.
# EDITOR: the editor / debug tools (the debug UI (ImGui), the collision view, the entity labels, the editor camera (EngineEditor/Editor.h) and the GL debug output).
set(EDITOR ON CACHE BOOL "Build the editor / debug tools" FORCE)
# The format families this game reads (SADX: Sonic Adventure, STH2005: Shadow the Hedgehog (2005)).
set(ILLUMINATION_FORMAT_SADX ON CACHE BOOL "Read the SADX formats" FORCE)
set(ILLUMINATION_FORMAT_STH2005 ON CACHE BOOL "Read the Shadow the Hedgehog (2005) formats" FORCE)

# The binary directory is given because the link points outside the source tree.
add_subdirectory(IluminationEngine ${CMAKE_BINARY_DIR}/IluminationEngine)

add_executable(SonicSunset
	src/main.cpp

	# The game's part of the engine (the seam, see docs/ARCHITECTURE.md): what the engine files include or call by fixed name.
	src/Game/GameEntity.cpp
	src/Game/GameLevel.cpp
	src/Game/GamePrefab.cpp
	src/Game/GameAPI.cpp
	src/Game/GameLevelLoader.cpp
	src/Game/GameLoaders.cpp
	src/Game/GameGraphics.cpp
	src/Game/GameScriptAPI.cpp
	src/Game/PlayerLoader.cpp
	src/Game/DebugSetup.cpp

	# The effects drawn after the entities
	src/Game/DashTrail.cpp
	src/Game/EffectBalls.cpp
	src/Game/DashSpheres.cpp
	src/Game/SonicTrail.cpp
)

if(EDITOR)
	target_sources(SonicSunset PRIVATE
		src/Game/AssetThumbnails.cpp
		src/Game/GamePrefabThumbnails.cpp
		src/Game/LibraryExport.cpp
	)
endif()

# The engine's include path carries the game's folders too: the engine's files find the game's headers (the seam) and the game's find the engine's.
target_include_directories(IluminationEngine PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/Game
)

target_link_libraries(SonicSunset PRIVATE IluminationEngine)

# The engine's shaders (Default, GLTF, SA, STH2005) are copied next to the executable, replacing existing files, on every build (see illumination_copy_directory in the engine's CMakeLists.txt).
illumination_copy_engine_assets(SonicSunset)

# The Lua scripts (Scripts/) are copied to Assets/Scripts the same way.
illumination_copy_directory(CopyScripts SonicSunset ${CMAKE_CURRENT_SOURCE_DIR}/Scripts Scripts)

# The levels (Levels/<name>/: <name>.json and the level's own Models, Textures, Collision, prefabs.json; read by Loading::loadLevel) are copied to Assets/Levels the same way.
illumination_copy_directory(CopyLevels SonicSunset ${CMAKE_CURRENT_SOURCE_DIR}/Levels Levels)

# The HubWorld (HubWorld.json: where each level sits in the one space, read by HubWorld::read) is copied to Assets/HubWorld.json.
illumination_copy_file(CopyHubWorld SonicSunset ${CMAKE_CURRENT_SOURCE_DIR}/HubWorld.json HubWorld.json)

# The asset catalog (Catalog/, made by tools/genAssetCatalog.py, read by the editor's Assets tab) is copied to Assets/Catalog the same way.
illumination_copy_directory(CopyCatalog SonicSunset ${CMAKE_CURRENT_SOURCE_DIR}/Catalog Catalog)

# The editor-wide ("Global") external assets (External/: Models, Textures, Prefabs/Global/prefabs.json; the level's own are in its Levels/<name>/ folder; read by Loading::scanExternalAssets) are copied to Assets/External the same way.
illumination_copy_directory(CopyExternal SonicSunset ${CMAKE_CURRENT_SOURCE_DIR}/External External)
