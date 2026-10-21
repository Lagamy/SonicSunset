#pragma once
#include "GameKeys.h"
#include "GLTFKeys.h"
#include "PoolId.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Loading
{
/*
The keys of a level made from its file (LevelLoader.cpp; the game reads its own engines' part of the file, Game/GameLevelLoader.h). The keys hold const char* and the instances keep pointers to them, so the strings live here and a record is never freed or moved (std::map nodes, std::deque).
skipped is the level file's "skippedTextures" (sadx) or "skippedEntries" (sth2005): our choice of what not to load, not part of the disc keys. The game's keys are in Game/GameLevelRecipe.inl.
*/
struct Recipe
{
	std::deque<std::string> strings;
	std::unique_ptr<GLTFLevelKey> gltfLevel;
	std::vector<std::string> skipped;
	glm::vec3 position{0.0f};
	PoolId shaderId;
#include "GameLevelRecipe.inl"
};

#ifdef EDITOR
/* The items of a comma separated list, trimmed of spaces; empty items are dropped (the New level fields). */
std::vector<std::string> splitList(const std::string& text_);
#endif
}
