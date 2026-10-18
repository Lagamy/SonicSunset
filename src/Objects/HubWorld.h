#pragma once
#include "PoolId.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

/*
The HubWorld: which levels sit side by side in one space and where (docs/TODO.md, level HubWorld). It is above Level: a level keeps its own local coordinates and files, and the HubWorld holds each level's place (HubWorldLevel) in the one space. The file is HubWorld.json
(Assets/HubWorld.json, copied from the source tree's HubWorld.json; the editor writes both): {"levels": [{"name", "position": [x, y, z], "rotation": [x, y, z]}], "entities": [{"name", "prefab", "position": [x, y, z], "rotation": [9 numbers], "scale": [x, y, z], "state": {...}}]}, level rotation in degrees (Level::hubWorldRotation); "entities" is optional. A missing file or an empty list is a HubWorld of no levels (Load HubWorld has nothing to load).
Two ways to load, kept apart: Loading::loadLevelAlone loads one level at its own origin (no HubWorld place, no neighbours; the editor's Load level, the scripts' sf.loadLevel); Loading::loadHubWorld loads every level of the HubWorld, each at its place (Level::setHubWorldTransform, given by Loading::loadLevel only while loadedAsHub is true; the editor's Load HubWorld, the scripts' sf.loadHubWorld).
active is the name of the level the editor edits (activeLevelId, API.h) and the Level tab shows; loaded are the levels loadHubWorld made beside the active one, which the next load of either kind removes first.
*/
struct HubWorldLevel
{
	std::string name;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(0.0f);
};

/*
An entity the HubWorld owns, not a level (Entity::hubWorldOwned): in HubWorld coordinates, made by Loading::loadHubWorld after the levels and removed by the next load of either kind. prefab is the prefab's name (Globals::Game::prefabs), empty = a plain entity. The rotation is the entity's 3x3 orientation matrix (the format of a level file's edits).
state is the rest of what the editor can edit, as JSON text in the format of a level file's "added" entry (behaviours, shapes, mesh, texture, externals; Loading.cpp syncHubWorldEntities writes it, buildHubWorldEntities applies it); empty = nothing. An entity value of a field is not kept (there is no level to number the entities by).
*/
struct HubWorldEntity
{
	std::string name;
	std::string prefab;
	glm::vec3 position = glm::vec3(0.0f);
	glm::mat3 rotation = glm::mat3(1.0f);
	glm::vec3 scale = glm::vec3(1.0f);
	std::string state;
	bool operator==(const HubWorldEntity& rOther_) const = default;
};

struct HubWorld
{
	std::vector<HubWorldLevel> levels;
	std::string active;
	/* True after loadHubWorld, false after loadLevelAlone: whether the loaded levels sit at their HubWorld places. */
	bool loadedAsHub = false;
	std::vector<PoolId> loaded;
	/* The file's entities (read, and written by write); Loading::syncHubWorldEntities refreshes them from the live ones before the editor writes. entityIds are the live ones made from them or by the editor. */
	std::vector<HubWorldEntity> entities;
	std::vector<PoolId> entityIds;

	/* Reads Assets/HubWorld.json; a missing file leaves the HubWorld empty, a wrong one is reported on stderr and leaves it empty. */
	void read();
	/* The entry of a level name, nullptr if the HubWorld has none. */
	const HubWorldLevel* find(const std::string& rName_) const;
	/* Adds or replaces the entry of the name. */
	void set(const HubWorldLevel& rLevel_);
	/* Removes the entry of the name, if there is one. */
	void remove(const std::string& rName_);
#ifdef EDITOR
	/* Writes HubWorld.json to Assets/ and to the source tree. Throws std::runtime_error if a file can't be written. */
	void write() const;
#endif
};
