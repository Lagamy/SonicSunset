#pragma once
#include "SASet.h"

/* The record of the game's data that places an object, handed to the script that sets a prefab's entity up (Scripting::setupPrefab; the seam between the engine and SonicFantasy, every game supplies its own). The engine reads type, rotation, position and scale from it. */
using PrefabPlacement = Ninja::SAPlacement;
