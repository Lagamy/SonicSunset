#include "GamePrefab.h"
#include "Prefab.h"
#include "Level.h"
#include "Entity.h"
#include "API.h"
#include "Globals.h"
#include "Scripting.h"
#include "LevelLoader.h"
#include "GameLevelLoader.h"
#include "ResourceKeys.h"
#include "PlayerLoader.h"
#include <cstdio>
#include <utility>
#include <stdexcept>

/* The part of Prefab and spawnPrefab that is the game's (the engine side is Objects/Prefab.cpp and Misc/API.cpp): the prefabs of the SADX and Shadow entity keys and of the two players, and how an entity is built from each. */

/* The Prefab of one of the game's kinds: its key (one of the two is null), what it is made of, and the group the editor sorts it into until Groups.json says otherwise. */
static void addGamePrefab(const char* name_, SourceEngineEnum game_, PrefabKind kind_, const SAEntityKey* pSAKey_, const STH2005::EntityKey* pONEKey_, bool hasModel_, bool hasCollision_, bool hasBehaviour_)
{
	Prefab prefab;
	prefab.name = name_;
	prefab.game = game_;
	prefab.group = kind_ == PrefabKind::SonicPlayer || kind_ == PrefabKind::ShadowPlayer ? PrefabGroup::Characters : PrefabGroup::StaticObjects;
	prefab.kind = kind_;
	prefab.saKey = pSAKey_;
	prefab.oneKey = pONEKey_;
	prefab.hasModel = hasModel_;
	prefab.hasCollision = hasCollision_;
	prefab.hasBehaviour = hasBehaviour_;
	addPrefab(std::move(prefab));
}

void registerPrefabs()
{
	/* The players: no entity key, they are made from the constants of ResourceKeys.h, and their body shape is their Changed file's (spawnPrefab). A script attaches their behaviours (attachPlayer). */
	addGamePrefab("Sonic", SA_NINJA, PrefabKind::SonicPlayer, nullptr, nullptr, true, true, true);
	addGamePrefab("Shadow", STH2005_ONE, PrefabKind::ShadowPlayer, nullptr, nullptr, true, true, true);

	/* The groups of the key prefabs are given afterwards, from Groups.json (Loading::loadPrefabData). */
	Loading::forEachSAEntityKey([](const SAEntityKey& rKey_)
	{
		bool hasModel = rKey_.model.rel != nullptr;
		for(const SAModelKey& rVariant : rKey_.variantModels) hasModel = hasModel || rVariant.rel != nullptr;
		bool hasCollision = !rKey_.collisions.empty() || !rKey_.variantCollisions.empty();
		addGamePrefab(rKey_.name, SA_NINJA, PrefabKind::SAObject, &rKey_, nullptr, hasModel, hasCollision, false);
	});
	Loading::forEachONEEntityKey([](const STH2005::EntityKey& rKey_)
	{
		addGamePrefab(rKey_.name, STH2005_ONE, PrefabKind::ONEObject, nullptr, &rKey_, !rKey_.models.empty(), !rKey_.collisions.empty(), false);
	});
}

/*
SADX object, see spawnPrefab: the prefab's model (or the variant the SET record picked, SpawnParams::pModel), the key's textures (untextured if it has none), the collision shapes if there are any, and the first motion the key lists, looping (without behaviour
nothing chooses a motion; the generator puts one that looks like an idle first). A motion that can't be read is only reported. Collision and the script behaviour only with inScene_ (see spawnPrefab).
*/
static void buildSAObject(Entity& rEntity_, PoolId shaderId_, const Prefab& rPrefab_, const SpawnParams& rParams_, bool inScene_)
{
    const SAEntityKey& rKey = *rPrefab_.saKey;
    const SAModelKey& rModel = rParams_.pModel != nullptr ? *rParams_.pModel : rKey.model;
    const std::vector<SACollisionShapesKey>& rCollisions = rParams_.pCollisions != nullptr ? *rParams_.pCollisions : rKey.collisions;
    if(rKey.textures.path != nullptr) rEntity_.setSAGPUInstance(shaderId_, rModel, rKey.textures);
    else rEntity_.setSAGPUInstance(shaderId_, rModel);
    /* An object with icon textures (O ITEMBOX) takes the one its record picks, by the same value as the variants (scale component variantField, a byte); without a record, the model's own. */
    if(!rKey.iconTextures.empty())
    {
        uint32_t value = rParams_.pPlacement == nullptr ? 0 : static_cast<uint32_t>(static_cast<int32_t>(rParams_.pPlacement->scale[rPrefab_.variantField]) & 0xFF);
        rEntity_.setSAIcon(rKey.iconTextures.front(), rKey.iconTextures[value % rKey.iconTextures.size()]);
    }
    if(inScene_)
    {
        if(!rCollisions.empty()) rEntity_.setSACollisionShapes(rCollisions);
        Scripting::setupPrefab(rEntity_.selfId, Globals::Game::prefabs.names[rEntity_.prefabId.id].c_str(), rParams_.pPlacement);
    }

    if(!rKey.motions.empty())
    {
        try
        {
            rEntity_.playSAMotion(rKey.motions[0]);
        }
        catch(const std::runtime_error& rError)
        {
            std::fprintf(stderr, "Spawn: %s %s has no motion: %s\n", rKey.name, rParams_.what.c_str(), rError.what());
        }
    }
}

/*
Shadow object, see spawnPrefab: one model of the key (SpawnParams::modelIndex, an index into EntityKey::models) with the key's textures and the collision shapes given (the key's if none are). The first motion the key lists is played on the first model
(index 0) only, looping for living things (Prefab::loopMotion) and otherwise once, holding its last frame (placeholder: for TANK and TURNOVER it is the "_on" one, a state change). A motion that can't be read is
reported and the entity stays posed at rest: the .MTP layout was decoded from SHADOW.MTP only. Collision and the script behaviour only with inScene_ (see spawnPrefab).
*/
static void buildONEObject(Entity& rEntity_, PoolId shaderId_, const Prefab& rPrefab_, const SpawnParams& rParams_, bool inScene_)
{
    const STH2005::EntityKey& rKey = *rPrefab_.oneKey;
    const std::vector<STH2005::CollisionKey>& rCollisions = rParams_.pONECollisions != nullptr ? *rParams_.pONECollisions : rKey.collisions;
    if(rKey.textures.entry != nullptr) rEntity_.setONEGPUInstance(shaderId_, rKey.models.at(rParams_.modelIndex), rKey.textures);
    else rEntity_.setONEGPUInstance(shaderId_, rKey.models.at(rParams_.modelIndex));
    if(inScene_)
    {
        if(!rCollisions.empty()) rEntity_.setONECollisionShapes(rCollisions);
        Scripting::setupPrefab(rEntity_.selfId, Globals::Game::prefabs.names[rEntity_.prefabId.id].c_str(), nullptr);
    }

    if(rParams_.modelIndex == 0 && !rKey.motions.empty())
    {
        try
        {
            rEntity_.playONEMotion(rKey.motions[0], rPrefab_.loopMotion);
        }
        catch(const std::runtime_error& rError)
        {
            std::fprintf(stderr, "Spawn: %s %s has no motion: %s\n", rKey.name, rParams_.what.c_str(), rError.what());
        }
    }
}

void buildGamePrefab(Entity& rEntity_, PoolId shaderId_, const Prefab& rPrefab_, const SpawnParams& rParams_, bool inScene_)
{
	Entity& rEntity = rEntity_;
	const bool inScene = inScene_;
	const PoolId shaderId = shaderId_;
	switch(rPrefab_.kind)
	{
	case PrefabKind::SAObject:
		buildSAObject(rEntity, shaderId, rPrefab_, rParams_, inScene);
		break;
	case PrefabKind::ONEObject:
		buildONEObject(rEntity, shaderId, rPrefab_, rParams_, inScene);
		break;
	case PrefabKind::SonicPlayer:
		rEntity.setSAGPUInstance(shaderId, Loading::sonicKeys().mesh, Loading::sonicKeys().textures);
		rEntity.playSAMotion(Loading::sonicKeys().idle);
		if(inScene)
		{
			rEntity.setPhysics(1.0f);
		}
		break;
	case PrefabKind::ShadowPlayer:
		rEntity.setONEGPUInstance(shaderId, Loading::shadowKeys().mesh, Loading::shadowKeys().textures);
		for(const STH2005::MotionKey& rMotion : Loading::shadowKeys().idle) rEntity.playONEMotion(rMotion);
		if(inScene)
		{
			rEntity.setPhysics(1.0f);
		}
		break;
	/* The engine builds this one itself (spawnPrefab). */
	case PrefabKind::External:
		break;
	}
}

void restoreGameSpawnParams(SpawnParams& rParams_, const EntityRecipe& rRecipe_)
{
	rParams_.pPlacement = rRecipe_.placement.get();
	rParams_.pONECollisions = rRecipe_.hasOneCollisions ? &rRecipe_.oneCollisions : nullptr;
}

void prepareThumbnailParams(const Prefab& rPrefab_, SpawnParams& rParams_)
{
	if(rPrefab_.kind != PrefabKind::SAObject || rPrefab_.saKey->model.rel != nullptr) return;
	for(const SAModelKey& rVariant : rPrefab_.saKey->variantModels)
	{
		if(rVariant.rel != nullptr)
		{
			rParams_.pModel = &rVariant;
			break;
		}
	}
}
