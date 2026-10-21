#pragma once
#include <cstdint>
#include "PoolId.h"
#include "lua.h"
#include "lauxlib.h"
#include <exception>
#include <glm/glm.hpp>

/* What the files of the scripting layer share (Scripting.cpp, ScriptAPI.cpp and the game's Game/GameScriptAPI.cpp); not for the rest of the engine. */
namespace Scripting
{
lua_State* state();

/* An entity, tag or behaviour as the integer a script holds: generation << 32 | id (-1 for UninitializedPoolId, whose two halves are all ones). */
inline int64_t toHandle(PoolId id_)
{
	return static_cast<int64_t>((static_cast<uint64_t>(id_.generation) << 32) | id_.id);
}

inline PoolId fromHandle(int64_t handle_)
{
	uint64_t bits = static_cast<uint64_t>(handle_);
	return PoolId{static_cast<uint32_t>(bits & 0xFFFFFFFFu), static_cast<uint32_t>(bits >> 32)};
}

/* The entity a handle names now: the handle's own entity if it exists, else the one that was made in its place by an in-place level reset (Scripting::entityRemade), else the handle's own PoolId (gone). Every function that takes an entity goes through it. */
PoolId currentEntity(PoolId id_);

/* The handle a script gets for an entity: the one it first had if a level reset made it again (so the handle a script kept compares equal to the one it is given now), else its own. Every place that gives a script an entity goes through it. */
int64_t entityHandle(PoolId id_);

/* Drops the redirects (entityRemade) whose newest entity is gone for good, so they do not pile up over many resets; the handle then names an entity that does not exist, as before. Called at the start of every tick, when no reset is half done. */
void pruneRemadeEntities();

/* Registers the function in the Lua registry (reference_, a luaL_ref) as the behaviour of a prefab (sf.prefabBehavior); a second one for the same name replaces the first, whose reference is released. */
void registerPrefabBehavior(const char* prefabName_, int reference_);

/* Pushes the entity's state table (sf.state), made as a tracked proxy on the first call: a write that changes a value in it, at any depth, sets Entity::scriptStateDirty. */
void pushEntityState(lua_State* pLua_, PoolId entityId_);

/* Registers the global table `sf`. */
void registerAPI(lua_State* pState_);

/* The helpers of the functions of `sf` (ScriptAPI.cpp). checkEntity: the live entity the handle at the argument names, a Lua error otherwise. */
PoolId checkEntity(lua_State* pLua_, int arg_);
int pushVector(lua_State* pLua_, const glm::vec3& rVector_);
glm::vec3 checkVector(lua_State* pLua_, int firstArg_);
PoolId shaderByName(const char* name_);
/* Remembers an entity a script made (Scripting::spawnedEntities) and pushes its handle; returns the number of values pushed (1). */
int pushSpawned(lua_State* pLua_, PoolId id_);

/* A C++ exception of the engine (a std::runtime_error from a pool or a loader) becomes a Lua error with its message instead of a crash. */
template<int (*Function)(lua_State*)>
int guarded(lua_State* pLua_)
{
	try
	{
		return Function(pLua_);
	}
	catch(const std::exception& rError)
	{
		return luaL_error(pLua_, "%s", rError.what());
	}
}

/* Defined by the game (Game/GameScriptAPI.cpp): adds its own functions to the table `sf` on the top of the stack. */
void registerGameAPI(lua_State* pLua_);
}
