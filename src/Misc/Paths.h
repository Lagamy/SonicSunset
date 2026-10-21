#pragma once

/*
Every path the game reads or writes, in one place. Relative paths are resolved from the working directory (the build folder), where the build copies the content into Assets/. Folder constants end with '/'; the *Dir ones do not (for std::filesystem::relative and directory_iterator). The Source* ones are the same content in the source tree (SF_SOURCE_DIR, defined by the editor build), where the editor saves edits and the next build copies them back to Assets/.
*/
namespace Paths
{
	inline constexpr const char* Levels = "Assets/Levels/";
	inline constexpr const char* LevelsDir = "Assets/Levels";
	inline constexpr const char* External = "Assets/External/";
	inline constexpr const char* ExternalDir = "Assets/External";
	inline constexpr const char* ExternalTextures = "Assets/External/Textures/";
	inline constexpr const char* ExternalModels = "Assets/External/Models/";
	inline constexpr const char* ExternalScripts = "Assets/External/Scripts/";
	inline constexpr const char* Players = "Assets/External/Players/";
	inline constexpr const char* Prefabs = "Assets/External/Prefabs/";
	inline constexpr const char* Scripts = "Assets/Scripts/";
	inline constexpr const char* ScriptsDir = "Assets/Scripts";
	inline constexpr const char* Shaders = "Assets/Shaders/";
	inline constexpr const char* HubWorld = "Assets/HubWorld.json";
	inline constexpr const char* Remaps = "Assets/External/Collision/Remaps.json";
	inline constexpr const char* BoundAreas = "Assets/External/Collision/BoundAreas.json";
	inline constexpr const char* CatalogFile = "Assets/Catalog/AssetCatalog.tsv";
	inline constexpr const char* FolderIcon = "Assets/Catalog/folder_icon.png";

#ifdef SF_SOURCE_DIR
	inline constexpr const char* SourceDir = SF_SOURCE_DIR;
	inline constexpr const char* SourceLevels = SF_SOURCE_DIR "/Levels/";
	inline constexpr const char* SourceExternal = SF_SOURCE_DIR "/External/";
	inline constexpr const char* SourceHubWorld = SF_SOURCE_DIR "/HubWorld.json";
	inline constexpr const char* SourceBoundAreas = SF_SOURCE_DIR "/External/Collision/BoundAreas.json";
	inline constexpr const char* SourceRemaps = SF_SOURCE_DIR "/External/Collision/Remaps.json";
#endif
}
