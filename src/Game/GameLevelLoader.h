#pragma once
#include "LevelLoader.h"
#include "LevelRecipe.h"
#include "GameKeys.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

struct Level;

/*
The part of the level loader that is the game's (the engine side is Loading/LevelLoader.cpp, which includes this file by name; every game supplies its own, defined in Game/GameLevelLoader.cpp): the engines a level file can name besides "gltf" ("sadx", "sth2005"), the generated tables it can
name, and the prefab data of the game's entity keys.
*/
namespace Loading
{
/* Reads the fields of a level file whose "engine" is one of the game's into the recipe; false (nothing read) if it is not. */
bool readGameRecipe(const std::string& engine_, Recipe& rRecipe_, const nlohmann::json& rFile_);

/* Gives a level of the game's (the recipe's keys) its instance, collision and placed entities; with keepInstance_ only the entities (resetLevel). */
void buildGameLevel(Level& rLevel_, Recipe& rRecipe_, bool keepInstance_);

#ifdef EDITOR
/* Writes the fields File > New level has for one of the game's engines into the level file; false (nothing written) if rLevel_.engine is not one of them. Throws std::runtime_error when a field it needs is empty. */
bool writeGameLevelFile(const NewLevelFile& rLevel_, nlohmann::ordered_json& rFile_);
#endif

/* Calls the function for every entity key of the generated entity tables the registry names (and the ones a stage's own code creates), each table in turn: the source of the prefabs (Prefab.h). The keys live for the whole run. */
void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_);
void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_);
}
