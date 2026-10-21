#pragma once
#include "GameEntity.h"
#include "GamePlacement.h"
#include "PoolId.h"
#include <cstdint>

/*
The part of Prefab and SpawnParams that is the game's (the seam between the engine and SonicFantasy; Prefab.h includes this file by name, and every game supplies its own). PrefabKind is what spawnPrefab (API.h) makes an entity of a prefab from; the engine needs External (a prefab made in the editor), and
hands every other kind to buildGamePrefab. The fields of Prefab and SpawnParams are in GamePrefabFields.inl and GameSpawnParams.inl.
*/

/*
How many entities one placement record of a Shadow object makes, and where they are (see RingLine.h): Single is one per model of the first variant at the record's position, RingLine is the layout of the ring object (a record
is a count of rings along a line, or the other shapes of the game's ring code), Variant is for objects whose record names one of the BIN's variants: the u32 at the start of its parameters is an index into Prefab::variants (observed
on the stage art objects; hypothesis that it is the index of the .BIN's info), and the record makes one entity per model of that variant.
*/
enum class PlacementLayout
{
	Single,
	RingLine,
	Variant
};

enum class PrefabKind
{
	SAObject,
	ONEObject,
	SonicPlayer,
	ShadowPlayer,
	External
};


inline constexpr PrefabKind DefaultPrefabKind = PrefabKind::SAObject;

/*
One rotation of an SADX object's display code (see Prefab::rotation): about axis ('X', 'Y' or 'Z') by the task's angle `field` (0 x, 1 y, 2 z, in BAMS), negated if negate, plus addend. A step with
field 3 uses no angle: it turns by addend alone.
*/
struct SARotationStep
{
	char axis;
	uint8_t field;
	int32_t addend;
	bool negate;
};


struct Entity;
struct Prefab;
struct SpawnParams;
struct EntityRecipe;

/* Defined in Game/GamePrefab.cpp: builds the entity of a prefab of one of the game's kinds (its instance, collision, motion and, with inScene_, the script behaviour). Throws std::runtime_error when it can't (spawnPrefab reports it and removes the entity). */
void buildGamePrefab(Entity& rEntity_, PoolId shaderId_, const Prefab& rPrefab_, const SpawnParams& rParams_, bool inScene_);

/* Defined in Game/GamePrefab.cpp: what the editor's thumbnail of a prefab changes in the params it spawns it with (for SADX, the first variant's model for a key that has only variants). */
void prepareThumbnailParams(const Prefab& rPrefab_, SpawnParams& rParams_);

/* Defined in Game/GamePrefab.cpp: puts back into the params of a Placed entity (Loading::resetLevel) the pointers its recipe holds copies of. */
void restoreGameSpawnParams(SpawnParams& rParams_, const EntityRecipe& rRecipe_);
