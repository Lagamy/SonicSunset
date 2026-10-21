#pragma once 
#include <string>
#include <unordered_map>
#include <memory>
#include "GLTFLevelInstance.h"
#include "Renderer.h"
#ifdef EDITOR
#include "Editor.h"
#endif
#include "Pool.h"
#include "Shader.h"
#include "BasicMesh.h"
#include "ExternalModel.h"
#include "Texture.h"
#include "PoolMap.h"
#include "GameGraphicsGlobals.h"

namespace Graphics 
{
	inline Renderer renderer;
	inline PoolMap<BasicMesh> basicMeshes = ("Meshes"); 
	inline PoolMap<Texture> textures = ("Textures");
	inline PoolMap<VertexLayout> vertexLayouts = ("VertexLayouts");
	inline ExternalPools externalProject("ProjectMeshes", "ProjectTextures", "ProjectModels");
	/*
	The level scope of the External assets: the pools of every loaded level by its name (made by Loading::scanExternalAssets, dropped by Loading::forgetExternalAssets when the level is destroyed, so an entity's pointer into
	its level's pools stays valid while the entity exists). externalNoLevel is the empty set used while no level was scanned; externalLevel() is the set of the active scope (the level scanned last).
	*/
	inline std::unordered_map<std::string, std::unique_ptr<ExternalPools>> externalLevels;
	inline ExternalPools externalNoLevel("LevelMeshes", "LevelTextures", "LevelModels");
	inline ExternalPools* pExternalLevel = &externalNoLevel;
	inline ExternalPools& externalLevel() { return *pExternalLevel; }
	inline PoolMap<Shader> shaders = ("Shaders");
	inline Pool<GLTFLevelInstance> gltfLevelInstances = ("GLTFLevelInstances");
}
