#pragma once
#include "PoolId.h"
#include "Paths.h"
#include <vector>

#include "GamePlacement.h"

/*
The scripting layer: gameplay written in Lua 5.4 (third-party/lua, compiled as C++ so a Lua error is a C++ exception and nothing is longjmp'ed over a C++ frame) over the engine, which stays C++: one physics and
collision system for both games, rendering, animation. A script sees the engine through the global table `sf` (ScriptAPI.cpp) and keeps what its objects remember in plain Lua tables (sf.state(entity)), so a new
field needs no recompile. Scripts are read from Assets/Scripts (the repo's Scripts folder, copied next to the executable by the build like the shaders): the games' own mechanics and entities and the core scripts. What the project added that no original game has (an extra entity with its own behaviour) is in Assets/External/Scripts (the repo's External/Scripts), loaded after the first by loadExternalScripts. init() runs nothing, main.cpp loads the entry scripts.
An entity, a tag or a behaviour reaches a script as an integer handle (its PoolId packed in 64 bits: generation << 32 | id), -1 (sf.none) for none; a handle of an entity that is gone makes the call that gets it fail,
sf.exists says. Only base, table, string, math, utf8 and coroutine libraries are open (no io, os, package or debug, and no dofile / loadfile): a script reaches the files and the machine only through sf.
A function a script registers (a behaviour, a collision interaction) that raises an error prints it with a traceback on stderr once and is not called again. Errors and what print writes are also kept in ScriptLog.h for the editor's Output tab.
*/
namespace Scripting
{
/* Creates the Lua state and the `sf` table; false if it already exists. Call before anything that needs a script (setupPrefab, attachPlayer), after BABehaviors::init (scripts find the built-in tags). */
void init();

/* Closes the state. Registry references held elsewhere (behaviour types) die with it; call at the end, once the game stops running. */
void destroy();

/* True between init and destroy. */
bool ready();

/* The two script roots (with the trailing slash). */
constexpr const char* ScriptsRoot = Paths::Scripts;
constexpr const char* ExternalScriptsRoot = Paths::ExternalScripts;

/* Runs a script below a root (path_ relative to it; Assets/Scripts by default); an error is printed on stderr and gives false. */
bool loadFile(const char* path_, const char* root_ = ScriptsRoot);

/* Runs Assets/External/Scripts/main.lua, the project's own scripts, then every .lua below its Entity/ and BoundArea/ folders (the folder says what a script is: sf.behavior or sf.onTouch; in name order) (main.lua includes the others with sf.include, which inside an External script is relative to Assets/External/Scripts); a missing file is nothing to load. Call right after Scripts/main.lua: the behaviours must be registered before registerPrefabs and the Changed files name them. */
void loadExternalScripts();

/* Called by the gameplay system and the collision system for a function a script registered (a Lua registry reference). callBehavior also gets the behaviour's PoolId: before its first call on an entity it writes the behaviour's declared fields into the entity's state table (EntityBehavior::fields). callInteraction also gets the index of the parent's shape that touched (-1: none, a level's triangle): the function's third argument is the table of that shape's values, nil for -1. */
void callBehavior(int scriptFunction_, PoolId entityId_, PoolId behaviorId_);
void callInteraction(int scriptFunction_, PoolId parentEntity_, PoolId contactEntity_, int parentShape_);

/*
A prefab's behaviour (sf.prefabBehavior(prefabName, fn), prefabName as Globals::Game::prefabs names it, e.g. "SADX/SPRING"): setupPrefab calls fn(entity, placement) for every entity spawnPrefab makes from that prefab, placement the SET record as a table {type, rotation = {x, y, z}
in BAMS, position = {x, y, z}, scale = {x, y, z}}, nil if the object has none; a prefab with none is skipped. hasPrefabBehavior says whether one is registered.
The other hook into the scripts' own functions is the global `attachPlayer(entity)`; a script that does not define it is skipped.
*/
void setupPrefab(PoolId entityId_, const char* prefabName_, const PrefabPlacement* pPlacement_);
bool hasPrefabBehavior(const char* prefabName_);
void attachPlayer(PoolId entityId_);

/* The entities scripts spawned (sf.spawnPlayer, sf.spawn), in order; some may be gone since (entityExists). The editor's Assets tab reads it to count what they draw as used by the level. */
const std::vector<PoolId>& spawnedEntities();

/* Resets the levels a script asked to reset (sf.resetLevel) and removes the ones it asked to unload (sf.unloadLevel); the gameplay system calls it at the start of every tick. */
void processPendingUnloads();

/*
Puts the game back to its start (the editor's Stop): the script's global resetGame(unloaded) is called with false, where it asks for its level to go (sf.unloadLevel); the removal is done here, now, since no tick
is running; then resetGame(true) is called, where it loads the level again and puts the players at its start. Both calls get the level's name as a second argument (nil without a name): a name makes the script load that level instead (the editor's File > Load level). A third argument says the kind of load: "level" (the level alone), "hub" (every level of HubWorld.json; the name is then the active one, empty = the first entry), nil = the kind the last load was (Stop). A script that does not define resetGame is skipped.
*/
void resetGame(const char* pLevelName_ = nullptr, const char* pMode_ = nullptr);

/*
Says that a level reset (Loading::resetLevel, or the editor's restore of what Play added) removed the entity oldId_ and made newId_ in its place from the same recipe. A script that kept the old handle (a global, the player's state) reaches the new entity with it: the functions of `sf`
that take an entity follow the handle (sf.exists is true), and every entity a script is given afterwards carries the first handle, so == against a kept one holds. oldId_ may be any id the entity had; the entity's later remakes are told the same way.
*/
void entityRemade(PoolId oldId_, PoolId newId_);

/* Gives back an entity's state table (Entity::scriptStateRef); Entity::destroy calls it. Safe after destroy(). */
void releaseState(int stateRef_);
}
