#pragma once
#include "GameKeys.h"
#include <cstdint>
#include <string>

/*
The part of Entity that is the game's (the seam between the engine and SonicFantasy; the engine includes this file by name from Entity.h, and every game supplies its own).
SourceEngineEnum: which engine's data the entity's instance comes from; selects the instance pool instanceId refers to. The engine needs GLTF_FILE, a Level's (glTF levels) and an external-only entity's (setExternalOnly): it has no instance, only an external model.
*/
enum SourceEngineEnum
{
	SA_NINJA,
	STH2005_ONE,
	GLTF_FILE
};

/* The engine of a new Entity or Level (aggregate-initialized with it in addEntity / addLevel). */
inline constexpr SourceEngineEnum DefaultSourceEngine = SA_NINJA;

/* The engines that have prefabs, in the order the editor lists them (its tabs and tiles index by the position here). */
inline constexpr SourceEngineEnum GameSources[] = {SA_NINJA, STH2005_ONE};
inline constexpr int GameSourceCount = 2;

/* The "engine" a File > New level starts with (the names a level file can have are the game's, Game/GameLevelLoader.h; "gltf" is the engine's). */
inline constexpr const char* DefaultLevelEngine = "sadx";

/* The folder of each engine's scripts under Scripts/ (the Assets tab). */
inline constexpr const char* GameScriptFolders[GameSourceCount] = {"sadx", "shadow"};

/* The position of the engine in GameSources, -1 if it is not one (GLTF_FILE). */
inline int gameSourceIndex(SourceEngineEnum game_)
{
	for(int i = 0; i < GameSourceCount; i++)
	{
		if(GameSources[i] == game_) return i;
	}
	return -1;
}

/* The name of the shader (Graphics::shaders) that draws the engine's instances. */
inline const char* gameShaderName(SourceEngineEnum game_)
{
	return game_ == SA_NINJA ? "SA" : "STH2005";
}

/* Whether the engine's collision shapes follow the SADX conventions (Physics::scaledShape, Ninja angles, the entity's rotation turns them); the other engines' do not. */
inline bool usesNinjaShapes(SourceEngineEnum game_)
{
	return game_ == SA_NINJA;
}

/* "SADX" / "STH2005" (the prefab names' prefix) and the folder names the editor shows. */
inline const char* prefabGameName(SourceEngineEnum game_)
{
	return game_ == SA_NINJA ? "SADX" : "STH2005";
}

inline const char* prefabGameFolder(SourceEngineEnum game_)
{
	return game_ == SA_NINJA ? "Sonic Adventure Director's Cut" : "Shadow The Hedgehog 2005";
}

/* The disc model name Entity::setDiscMesh takes for the model the editor's choice names (MeshChoice::name): a Shadow archive's models are named "(mesh)/<entry>" there. */
inline std::string gameDiscMeshName(SourceEngineEnum game_, const std::string& rName_)
{
	return game_ == STH2005_ONE ? "(mesh)/" + rName_ : rName_;
}

namespace Graphics
{
struct PoseRequest;
}

/*
The motion an entity wants to play, set by gameplay (changeAnimation in API.h) and applied by Entity::update once a frame, only when it differs from the one last applied. At most one of the two is set (the
engine of the entity's instance says which). The keys are not copied: they are static data.
*/
struct AnimationIntent
{
	const SAMotionKey* pSAMotion = nullptr;
	const STH2005::MotionKey* pONEMotion = nullptr;
	bool loop = true;
	/* An SA entity: the mesh to draw with the motion (switched first, since a motion animates the nodes of one model), nullptr = keep the current one. */
	const SAModelKey* pSAMesh = nullptr;

	bool operator==(const AnimationIntent&) const = default;
};
