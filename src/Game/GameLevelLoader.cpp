#include "GameLevelLoader.h"
#include "ResourceKeys.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "Level.h"
#include "Globals.h"
#include "Paths.h"
#include "Prefab.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <utility>
#include <map>
#include <format>
#include <stdexcept>

namespace Loading
{
namespace
{
using Json = nlohmann::json;

/*
The generated tables a level file can name (see the "entityTable" and "typeNames" fields in docs/ENGINE_USAGE.md). A new generated
table is one line here.
*/
const std::map<std::string, const std::vector<SAPlacedEntityKey>*> saEntityTables{
	{"SAEntityTable_EmeraldCoast", &SAEntityTable_EmeraldCoast}
};

/* Objects a stage's own code creates (no SET record): their keys, so they are prefabs; the level file's "source" says where they stand. */
const std::vector<const SAEntityKey*> saStageCreated{
	&EmeraldCoastBridgeDockForward, &EmeraldCoastBridgeDockBackward, &EmeraldCoastBridgePier, &EmeraldCoastBridgeLinks
};
const std::map<std::string, const std::map<uint16_t, const char*>*> typeNameMaps{
	{"SAObjectTypeNames_EmeraldCoast", &SAObjectTypeNames_EmeraldCoast},
	{"ObjectTypeNames", &ObjectTypeNames}
};
const std::map<std::string, const std::vector<STH2005::PlacedEntityKey>*> oneEntityTables{
	{"EntityTable_stg0100", &EntityTable_stg0100}
};

const char* keepString(Recipe& rRecipe_, const std::string& value_)
{
	rRecipe_.strings.push_back(value_);
	return rRecipe_.strings.back().c_str();
}

template<typename Table>
const Table& findTable(const std::map<std::string, const Table*>& rTables_, const Json& rFile_, const char* field_)
{
	std::string name = rFile_.at(field_).get<std::string>();
	auto found = rTables_.find(name);
	if(found == rTables_.end()) throw std::runtime_error(std::format("'{}' is not a table LevelLoader.cpp knows ({}).", name, field_));
	return *found->second;
}

std::vector<const char*> keepStrings(Recipe& rRecipe_, const Json& rFile_, const char* field_)
{
	std::vector<const char*> result;
	if(!rFile_.contains(field_)) return result;
	for(const Json& rValue : rFile_.at(field_)) result.push_back(keepString(rRecipe_, rValue.get<std::string>()));
	return result;
}

/* A number, or a string in any base ("0x1B8D68"): offsets are written in hex, like everywhere in the docs. */
uint32_t readOffset(const Json& rValue_)
{
	if(rValue_.is_string()) return static_cast<uint32_t>(std::stoul(rValue_.get<std::string>(), nullptr, 0));
	return rValue_.get<uint32_t>();
}

void fillSA(Recipe& rRecipe_, const Json& rFile_)
{
	const Json& rLandTable = rFile_.at("landTable");
	if(rFile_.contains("skippedTextures")) rRecipe_.skipped = rFile_.at("skippedTextures").get<std::vector<std::string>>();
	rRecipe_.saLandTable = std::make_unique<SALandTableKey>(SALandTableKey{keepString(rRecipe_, rLandTable.at("rel").get<std::string>()), readOffset(rLandTable.at("offset")), keepStrings(rRecipe_, rFile_, "placementFiles")});
	SALandTableKey& rKey = *rRecipe_.saLandTable;
	if(rFile_.contains("entityTable")) rKey.entities = findTable(saEntityTables, rFile_, "entityTable");
	if(rFile_.contains("typeNames")) rKey.typeNames = &findTable(typeNameMaps, rFile_, "typeNames");
	rRecipe_.saTextures = std::make_unique<SATextureSetKey>(SATextureSetKey{keepString(rRecipe_, rFile_.at("textures").get<std::string>())});
	if(rFile_.contains("replacedTextures"))
	{
		for(const Json& rEntry : rFile_.at("replacedTextures")) rRecipe_.saReplacedTextures.push_back(SATextureReplacement{rEntry.at("name").get<std::string>(), rEntry.at("with").get<std::string>(), rEntry.at("brightness").get<float>()});
	}
}

void fillONE(Recipe& rRecipe_, const Json& rFile_)
{
	std::vector<STH2005::ArchiveKey> sections;
	for(const Json& rSection : rFile_.at("sections")) sections.push_back(STH2005::ArchiveKey{keepString(rRecipe_, rSection.get<std::string>())});
	const Json& rTextures = rFile_.at("textures");
	STH2005::TextureKey textures{{keepString(rRecipe_, rTextures.at("archive").get<std::string>())}, keepString(rRecipe_, rTextures.at("txd").get<std::string>())};
	if(rFile_.contains("skippedEntries")) rRecipe_.skipped = rFile_.at("skippedEntries").get<std::vector<std::string>>();
	rRecipe_.oneLevel = std::make_unique<STH2005::LevelKey>(STH2005::LevelKey{sections, textures, keepStrings(rRecipe_, rFile_, "placementFiles")});
	STH2005::LevelKey& rKey = *rRecipe_.oneLevel;
	if(rFile_.contains("entityTable")) rKey.entities = findTable(oneEntityTables, rFile_, "entityTable");
	if(rFile_.contains("typeNames")) rKey.typeNames = &findTable(typeNameMaps, rFile_, "typeNames");
}
}

bool readGameRecipe(const std::string& engine_, Recipe& rRecipe_, const nlohmann::json& rFile_)
{
	if(engine_ == "sadx") fillSA(rRecipe_, rFile_);
	else if(engine_ == "sth2005") fillONE(rRecipe_, rFile_);
	else return false;
	return true;
}

void buildGameLevel(Level& rLevel_, Recipe& rRecipe_, bool keepInstance_)
{
	if(rRecipe_.saLandTable != nullptr)
	{
		if(!keepInstance_)
		{
			rLevel_.setSAGPUInstance(rRecipe_.shaderId, *rRecipe_.saLandTable, *rRecipe_.saTextures, rRecipe_.skipped, rRecipe_.saReplacedTextures);
			rLevel_.setSACollision(*rRecipe_.saLandTable);
		}
		rLevel_.setSAEntities(rRecipe_.shaderId, *rRecipe_.saLandTable);
	}
	else
	{
		if(!keepInstance_)
		{
			rLevel_.setONEGPUInstance(rRecipe_.shaderId, *rRecipe_.oneLevel, rRecipe_.skipped);
			rLevel_.setONECollision(*rRecipe_.oneLevel);
		}
		rLevel_.setONEEntities(rRecipe_.shaderId, *rRecipe_.oneLevel);
	}
}

#ifdef EDITOR
bool writeGameLevelFile(const NewLevelFile& rLevel_, nlohmann::ordered_json& rFile_)
{
	nlohmann::ordered_json& file = rFile_;
	if(rLevel_.engine == "sadx")
	{
		if(rLevel_.landTableRel.empty() || rLevel_.landTableOffset.empty() || rLevel_.textures.empty()) throw std::runtime_error("an sadx level needs the land table's module and offset and the textures.");
		file["landTable"] = {{"rel", rLevel_.landTableRel}, {"offset", rLevel_.landTableOffset}};
		file["textures"] = rLevel_.textures;
	}
	else if(rLevel_.engine == "sth2005")
	{
		std::vector<std::string> sections = splitList(rLevel_.sections);
		if(sections.empty() || rLevel_.textures.empty() || rLevel_.txd.empty()) throw std::runtime_error("an sth2005 level needs sections and the texture archive with its txd.");
		file["sections"] = sections;
		file["textures"] = {{"archive", rLevel_.textures}, {"txd", rLevel_.txd}};
	}
	else return false;

	/* The placement files, entity table and type names are disc formats. */
	std::vector<std::string> placementFiles = splitList(rLevel_.placementFiles);
	if(!placementFiles.empty()) file["placementFiles"] = placementFiles;
	if(!rLevel_.entityTable.empty()) file["entityTable"] = rLevel_.entityTable;
	if(!rLevel_.typeNames.empty()) file["typeNames"] = rLevel_.typeNames;
	return true;
}
#endif

void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : saEntityTables)
	{
		for(const SAPlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
	for(const SAEntityKey* pKey : saStageCreated) rFunction_(*pKey);
}

void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : oneEntityTables)
	{
		for(const STH2005::PlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
}

namespace
{
PrefabGroup readGroup(const std::string& rName_)
{
	for(PrefabGroup group : {PrefabGroup::Enemies, PrefabGroup::Npcs, PrefabGroup::Characters, PrefabGroup::StaticObjects, PrefabGroup::InteractibleObjects})
	{
		if(rName_ == prefabGroupFolder(group)) return group;
	}
	throw std::runtime_error(std::format("'{}' is not a prefab group.", rName_));
}

/* The group a key name goes in for one game of Groups.json: the first prefix that starts it, else the list that names it, else StaticObjects. */
PrefabGroup groupOf(const Json& rGame_, const std::string& rName_)
{
	for(const auto& [prefix, group] : rGame_.at("prefixes").items())
	{
		if(rName_.starts_with(prefix)) return readGroup(group.get<std::string>());
	}
	for(const auto& [group, names] : rGame_.at("names").items())
	{
		for(const Json& rName : names)
		{
			if(rName.get<std::string>() == rName_) return readGroup(group);
		}
	}
	return PrefabGroup::StaticObjects;
}

PlacementLayout readLayout(const std::string& rName_)
{
	if(rName_ == "Single") return PlacementLayout::Single;
	if(rName_ == "RingLine") return PlacementLayout::RingLine;
	if(rName_ == "Variant") return PlacementLayout::Variant;
	throw std::runtime_error(std::format("'{}' is not a placement layout.", rName_));
}
}

void loadPrefabData()
{
	auto& rPrefabs = Globals::Game::prefabs;
	try
	{
		std::ifstream stream(std::string(Paths::Prefabs) + "Groups.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json groups = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i]) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			if(rPrefab.kind != PrefabKind::SAObject && rPrefab.kind != PrefabKind::ONEObject) continue;
			rPrefab.group = groupOf(groups.at(prefabGameName(rPrefab.game)), rPrefab.name);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Prefab groups not read (Assets/External/Prefabs/Groups.json): %s\n", rError.what());
	}
	try
	{
		std::ifstream stream(std::string(Paths::Prefabs) + "STH2005/Placement.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json placement = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i] || rPrefabs.objects[i].kind != PrefabKind::ONEObject) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			auto found = placement.find(rPrefab.name);
			if(found == placement.end()) continue;
			rPrefab.layout = readLayout(found->value("layout", std::string("Single")));
			rPrefab.variants = found->value("variants", std::vector<std::vector<uint32_t>>{});
			rPrefab.loopMotion = found->value("loopMotion", false);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Prefab placement data not read (Assets/External/Prefabs/STH2005/Placement.json): %s\n", rError.what());
	}
	try
	{
		std::ifstream stream(std::string(Paths::Prefabs) + "SADX/Placement.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json placement = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i] || rPrefabs.objects[i].kind != PrefabKind::SAObject) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			auto found = placement.find(rPrefab.saKey->name);
			if(found == placement.end()) continue;
			for(const Json& rStep : found->value("rotation", Json::array()))
			{
				std::string axis = rStep.at("axis").get<std::string>();
				if(axis.size() != 1 || axis.find_first_of("XYZ") != 0) throw std::runtime_error(std::format("{}: '{}' is not an axis.", rPrefab.name, axis));
				rPrefab.rotation.push_back(SARotationStep{axis[0], rStep.at("field").get<uint8_t>(), rStep.at("addend").get<int32_t>(), rStep.at("negate").get<bool>()});
			}
			rPrefab.variantField = found->value("variantField", 0);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "SADX prefab placement data not read (Assets/External/Prefabs/SADX/Placement.json): %s\n", rError.what());
	}
}
}
