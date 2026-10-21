#include "LevelLoader.h"
#include "Paths.h"
#include "LevelRecipe.h"
#include "GameLevelLoader.h"
#include "Level.h"
#include "API.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "Prefab.h"
#include "Scripting.h"
#ifdef EDITOR
#include "Editor.h"
#include "ExternalAssets.h"
#include "ExternalPrefabs.h"
#include "GLTFImport.h"
#endif
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <format>

namespace Loading
{
namespace
{
using Json = nlohmann::json;

/* The recipe of each level loaded so far, by name: the keys the instances keep pointers into. */
std::map<std::string, Recipe> recipes;

/* The level's visual is every chunk in its folder; "collisions" names the collision files (Collision/<name>.collision.json) that make up its collision. */
void fillGLTF(Recipe& rRecipe_, const Json& rFile_, const std::string& rName_)
{
	rRecipe_.gltfLevel = std::make_unique<GLTFLevelKey>();
	rRecipe_.gltfLevel->levelName = rName_;
	if(rFile_.contains("collisions")) rRecipe_.gltfLevel->collisions = rFile_.at("collisions").get<std::vector<std::string>>();
}

/* A level is a folder: <root><name>/<name>.json next to the level's own Models, Textures, Collision and Prefabs (root: "Assets/Levels/", or the source tree's Levels/ in the editor). */
std::string levelFilePath(const std::string& root_, const std::string& name_)
{
	return root_ + name_ + "/" + name_ + ".json";
}

/* Reads Assets/Levels/<name>/<name>.json into the recipe of the name, once. */
void readRecipe(const std::string& name_, Recipe& rRecipe_)
{
	std::string path = levelFilePath(Paths::Levels, name_);
	std::ifstream stream(path);
	if(!stream) throw std::runtime_error(std::format("unknown level '{}' ({} not found).", name_, path));
	Json file = Json::parse(stream);
	std::string engine = file.at("engine").get<std::string>();
	rRecipe_.shaderId = Graphics::shaders.nameToId.at(file.at("shader").get<std::string>());
	if(file.contains("position"))
	{
		std::vector<float> position = file.at("position").get<std::vector<float>>();
		if(position.size() != 3) throw std::runtime_error("position needs 3 numbers.");
		rRecipe_.position = glm::vec3(position[0], position[1], position[2]);
	}
	if(engine == "gltf") fillGLTF(rRecipe_, file, name_);
	else if(!readGameRecipe(engine, rRecipe_, file)) throw std::runtime_error(std::format("unknown engine '{}' (\"gltf\" or one of the game's).", engine));
}
}

namespace
{
/* The position of a level file's edit, a world position. */
glm::vec3 readPosition(const Json& rValue_)
{
	std::vector<float> position = rValue_.get<std::vector<float>>();
	if(position.size() != 3) throw std::runtime_error("an edit's position needs 3 numbers.");
	return glm::vec3(position[0], position[1], position[2]);
}

/* A level file edit's rotation: the 9 numbers of the entity's 3x3 orientation matrix, column by column. */
glm::mat4 readRotation(const Json& rValue_)
{
	std::vector<float> values = rValue_.get<std::vector<float>>();
	if(values.size() != 9) throw std::runtime_error("an edit's rotation needs 9 numbers.");
	return glm::mat4(glm::make_mat3(values.data()));
}

/*
One external value as the level file has it (writeExternals, writeShape): a number, bool or string as itself, a vec3 as [x, y, z], an entity as its index in the level (Level::editIndexOf; null if it is not one of the level's or no level is given) and a prefab as its
name (null: none).
*/
nlohmann::ordered_json writeExternalValue(ExternalType type_, const ExternalValue& rValue_, const Level* pLevel_)
{
	switch(type_)
	{
	case ExternalType::Number:
		return rValue_.number;
	case ExternalType::Bool:
		return rValue_.flag;
	case ExternalType::String:
		return rValue_.text;
	case ExternalType::Vec3:
		return nlohmann::ordered_json::array({rValue_.vector.x, rValue_.vector.y, rValue_.vector.z});
	case ExternalType::Entity:
	{
		int32_t index = pLevel_ ? pLevel_->editIndexOf(rValue_.entity) : -1;
		return index >= 0 ? nlohmann::ordered_json(index) : nlohmann::ordered_json(nullptr);
	}
	case ExternalType::Prefab:
	{
		auto& rPrefabs = Globals::Game::prefabs;
		bool exists = rValue_.prefab.id < rPrefabs.objects.size() && rPrefabs.alive[rValue_.prefab.id] && rPrefabs.generation[rValue_.prefab.id] == rValue_.prefab.generation;
		return exists ? nlohmann::ordered_json(rPrefabs.names[rValue_.prefab.id]) : nlohmann::ordered_json(nullptr);
	}
	case ExternalType::Level:
		return rValue_.text.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(rValue_.text);
	}
	return nullptr;
}

/* The value of writeExternalValue; an entity is left as entityIndex. Throws std::exception if the JSON does not fit the type (or names an unknown prefab). */
ExternalValue readExternalValue(ExternalType type_, const Json& rJson_)
{
	ExternalValue value;
	switch(type_)
	{
	case ExternalType::Number:
		value.number = rJson_.get<double>();
		break;
	case ExternalType::Bool:
		value.flag = rJson_.get<bool>();
		break;
	case ExternalType::String:
		value.text = rJson_.get<std::string>();
		break;
	case ExternalType::Vec3:
		value.vector = readPosition(rJson_);
		break;
	case ExternalType::Entity:
		if(!rJson_.is_null()) value.entityIndex = rJson_.get<int32_t>();
		break;
	case ExternalType::Prefab:
		if(!rJson_.is_null())
		{
			auto prefab = Globals::Game::prefabs.nameToId.find(rJson_.get<std::string>());
			if(prefab == Globals::Game::prefabs.nameToId.end()) throw std::runtime_error("unknown prefab " + rJson_.get<std::string>());
			value.prefab = prefab->second;
		}
		break;
	case ExternalType::Level:
		if(!rJson_.is_null()) value.text = rJson_.get<std::string>();
		break;
	}
	return value;
}

/*
A bound area shape as the level file has it: every field of Physics::CollisionShape, the behaviour by name (null: none), and "values" (only when the shape has any): the shape's external values of the fields its behaviour declares (writeExternalValue; an entity
needs pLevel_, without one it is null), then what the file had that no field took (valuesKept) as it was.
*/
nlohmann::ordered_json writeShape(const Physics::CollisionShape& rShape_, const Level* pLevel_ = nullptr)
{
	nlohmann::ordered_json json;
	json["kind"] = rShape_.kind;
	json["form"] = rShape_.form;
	json["push"] = rShape_.push;
	json["damage"] = rShape_.damage;
	json["attr"] = rShape_.attr;
	json["center"] = nlohmann::ordered_json::array({rShape_.center.x, rShape_.center.y, rShape_.center.z});
	json["a"] = rShape_.a;
	json["b"] = rShape_.b;
	json["c"] = rShape_.c;
	json["d"] = rShape_.d;
	json["angle"] = nlohmann::ordered_json::array({rShape_.angle.x, rShape_.angle.y, rShape_.angle.z});
	if(rShape_.behavior == UninitializedPoolId) json["behavior"] = nullptr;
	else json["behavior"] = Globals::Game::behaviors.names[rShape_.behavior.id];
	if(!rShape_.tags.empty())
	{
		nlohmann::ordered_json tags = nlohmann::ordered_json::array();
		for(PoolId tag : rShape_.tags) tags.push_back(Globals::Game::baTags.names[tag.id]);
		json["tags"] = tags;
	}
	nlohmann::ordered_json values = nlohmann::ordered_json::object();
	for(const ExternalField& rField : Globals::Game::baBehaviors.fieldsOf(rShape_.behavior))
	{
		auto found = rShape_.values.find(rField.name);
		if(found != rShape_.values.end()) values[rField.name] = writeExternalValue(rField.type, found->second, pLevel_);
	}
	for(const auto& [fieldName, rText] : rShape_.valuesKept)
	{
		if(!values.contains(fieldName)) values[fieldName] = nlohmann::ordered_json::parse(rText);
	}
	if(!values.empty()) json["values"] = values;
	return json;
}

/* A texture choice as {"kind": "default"|"disc"|"external", "name": ...}. */
nlohmann::ordered_json writeTextureChoice(const TextureChoice& rChoice_)
{
	const char* kind = rChoice_.kind == TextureChoiceKind::Disc ? "disc" : rChoice_.kind == TextureChoiceKind::External ? "external" : "default";
	return {{"kind", kind}, {"name", rChoice_.name}};
}

TextureChoice readTextureChoice(const Json& rJson_)
{
	std::string kind = rJson_.at("kind").get<std::string>();
	TextureChoice choice;
	if(kind == "disc") choice.kind = TextureChoiceKind::Disc;
	else if(kind == "external") choice.kind = TextureChoiceKind::External;
	else if(kind != "default") throw std::runtime_error(std::format("a texture's kind '{}' is not default, disc or external.", kind));
	choice.name = rJson_.value("name", std::string());
	return choice;
}

/* A model choice as {"kind": "default"|"disc"|"external", "name": ...}. */
nlohmann::ordered_json writeMeshChoice(const MeshChoice& rChoice_)
{
	const char* kind = rChoice_.kind == MeshChoiceKind::Disc ? "disc" : rChoice_.kind == MeshChoiceKind::External ? "external" : "default";
	return {{"kind", kind}, {"name", rChoice_.name}};
}

MeshChoice readMeshChoice(const Json& rJson_)
{
	std::string kind = rJson_.at("kind").get<std::string>();
	MeshChoice choice;
	if(kind == "disc") choice.kind = MeshChoiceKind::Disc;
	else if(kind == "external") choice.kind = MeshChoiceKind::External;
	else if(kind != "default") throw std::runtime_error(std::format("a model's kind '{}' is not default, disc or external.", kind));
	choice.name = rJson_.value("name", std::string());
	return choice;
}

/* Gives an entity an edit's model; one that can't be used (an external model that is gone, a disc model that does not fit the entity) is reported on stderr and the entity keeps its own. */
void applyMeshChoice(PoolId entityId_, const MeshChoice& rChoice_, const std::string& rLevel_)
{
	try
	{
		setEntityMesh(entityId_, rChoice_);
	}
	catch(const std::runtime_error& rError)
	{
		fprintf(stderr, "Level '%s': an edit's model '%s' is not used: %s.\n", rLevel_.c_str(), rChoice_.name.c_str(), rError.what());
	}
}

/* Gives an entity an edit's texture; a texture that can't be used (an external file that is gone, a disc texture that does not fit the model) is reported on stderr and the entity keeps its own. */
void applyTextureChoice(PoolId entityId_, const TextureChoice& rChoice_, const std::string& rLevel_)
{
	try
	{
		setEntityTexture(entityId_, rChoice_);
	}
	catch(const std::runtime_error& rError)
	{
		fprintf(stderr, "Level '%s': an edit's texture '%s' is not used: %s.\n", rLevel_.c_str(), rChoice_.name.c_str(), rError.what());
	}
}

/* The shape of writeShape; a collision behaviour name nothing registered is reported on stderr and the shape gets none. */
Physics::CollisionShape readShape(const Json& rJson_, const std::string& rLevel_)
{
	Physics::CollisionShape shape{};
	shape.kind = rJson_.at("kind").get<uint8_t>();
	shape.form = rJson_.at("form").get<uint8_t>();
	shape.push = rJson_.at("push").get<uint8_t>();
	shape.damage = rJson_.at("damage").get<uint8_t>();
	shape.attr = rJson_.at("attr").get<uint32_t>();
	shape.center = readPosition(rJson_.at("center"));
	shape.a = rJson_.at("a").get<float>();
	shape.b = rJson_.at("b").get<float>();
	shape.c = rJson_.at("c").get<float>();
	shape.d = rJson_.at("d").get<float>();
	std::vector<int32_t> angle = rJson_.at("angle").get<std::vector<int32_t>>();
	if(angle.size() != 3) throw std::runtime_error("a shape's angle needs 3 numbers.");
	shape.angle = glm::ivec3(angle[0], angle[1], angle[2]);
	shape.behavior = UninitializedPoolId;
	if(!rJson_.at("behavior").is_null())
	{
		std::string name = rJson_.at("behavior").get<std::string>();
		auto found = Globals::Game::behaviors.nameToId.find(name);
		if(found == Globals::Game::behaviors.nameToId.end()) fprintf(stderr, "Level '%s': an edit names the unknown collision behaviour '%s'.\n", rLevel_.c_str(), name.c_str());
		else shape.behavior = found->second;
	}
	if(rJson_.contains("tags"))
	{
		for(const nlohmann::json& rTag : rJson_.at("tags"))
		{
			PoolId tag = Globals::Game::baBehaviors.findTag(rTag.get<std::string>().c_str());
			if(tag == UninitializedPoolId) fprintf(stderr, "Level '%s': an edit names the unknown collision tag '%s'.\n", rLevel_.c_str(), rTag.get<std::string>().c_str());
			else shape.tags.push_back(tag);
		}
	}
	if(rJson_.contains("values"))
	{
		const std::vector<ExternalField>& rDeclared = Globals::Game::baBehaviors.fieldsOf(shape.behavior);
		for(const auto& [fieldName, rJson] : rJson_.at("values").items())
		{
			auto field = std::find_if(rDeclared.begin(), rDeclared.end(), [&](const ExternalField& rField_) { return rField_.name == fieldName; });
			if(field == rDeclared.end())
			{
				fprintf(stderr, "Level '%s': a shape sets the value '%s', which its behaviour does not declare; kept as it is.\n", rLevel_.c_str(), fieldName.c_str());
				shape.valuesKept[fieldName] = rJson.dump();
				continue;
			}
			try
			{
				shape.values[fieldName] = readExternalValue(field->type, rJson);
			}
			catch(const std::exception& rError)
			{
				fprintf(stderr, "Level '%s': the shape value '%s' does not fit: %s; kept as it is.\n", rLevel_.c_str(), fieldName.c_str(), rError.what());
				shape.valuesKept[fieldName] = rJson.dump();
			}
		}
	}
	return shape;
}

/* The behaviour with that name, UninitializedPoolId (reported on stderr) if no script registered it. */
PoolId findBehavior(const std::string& rName_, const std::string& rLevel_)
{
	auto found = Globals::Game::entityBehaviorTypes.nameToId.find(rName_);
	if(found == Globals::Game::entityBehaviorTypes.nameToId.end())
	{
		fprintf(stderr, "Level '%s': an edit names the unknown behaviour '%s'.\n", rLevel_.c_str(), rName_.c_str());
		return UninitializedPoolId;
	}
	return found->second;
}

/*
A prefab's edits as a level file's "prefabs" entry and a Changed file have them: {"added": [behaviour names], "removed": [...], "shapesAdded": [shapes], "shapesRemoved": [...], "mesh": choice, "texture": choice}. The model and texture are
written only when withModel_ (an External prefab's are its definition, saved with the prefab).
*/
nlohmann::ordered_json writePrefabEdits(const PrefabEdits& rEdits_, bool withModel_)
{
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	nlohmann::ordered_json lists;
	lists["added"] = nlohmann::ordered_json::array();
	lists["removed"] = nlohmann::ordered_json::array();
	for(PoolId behaviorId : rEdits_.addedBehaviors) lists["added"].push_back(rTypes.names[behaviorId.id]);
	for(PoolId behaviorId : rEdits_.removedBehaviors) lists["removed"].push_back(rTypes.names[behaviorId.id]);
	lists["shapesAdded"] = nlohmann::ordered_json::array();
	lists["shapesRemoved"] = nlohmann::ordered_json::array();
	for(const Physics::CollisionShape& rShape : rEdits_.addedShapes) lists["shapesAdded"].push_back(writeShape(rShape));
	for(const Physics::CollisionShape& rShape : rEdits_.removedShapes) lists["shapesRemoved"].push_back(writeShape(rShape));
	if(withModel_ && rEdits_.mesh.kind != MeshChoiceKind::Default) lists["mesh"] = writeMeshChoice(rEdits_.mesh);
	if(withModel_ && rEdits_.texture.kind != TextureChoiceKind::Default) lists["texture"] = writeTextureChoice(rEdits_.texture);
	return lists;
}

bool prefabEditsEmpty(const PrefabEdits& rEdits_, bool withModel_)
{
	bool lists = rEdits_.addedBehaviors.empty() && rEdits_.removedBehaviors.empty() && rEdits_.addedShapes.empty() && rEdits_.removedShapes.empty();
	return lists && (!withModel_ || (rEdits_.texture.kind == TextureChoiceKind::Default && rEdits_.mesh.kind == MeshChoiceKind::Default));
}

/* Fills the set from what writePrefabEdits wrote, without touching any entity (Changed files are read before any exists). A name nothing registered is reported (rWhere_ names the file) and skipped. */
void readPrefabEdits(PrefabEdits& rEdits_, const Json& rJson_, const std::string& rWhere_)
{
	for(const Json& rName : rJson_.value("added", Json::array()))
	{
		PoolId behaviorId = findBehavior(rName.get<std::string>(), rWhere_);
		if(behaviorId != UninitializedPoolId) rEdits_.addedBehaviors.push_back(behaviorId);
	}
	for(const Json& rName : rJson_.value("removed", Json::array()))
	{
		PoolId behaviorId = findBehavior(rName.get<std::string>(), rWhere_);
		if(behaviorId != UninitializedPoolId) rEdits_.removedBehaviors.push_back(behaviorId);
	}
	for(const Json& rShape : rJson_.value("shapesAdded", Json::array())) rEdits_.addedShapes.push_back(readShape(rShape, rWhere_));
	for(const Json& rShape : rJson_.value("shapesRemoved", Json::array())) rEdits_.removedShapes.push_back(readShape(rShape, rWhere_));
	if(rJson_.contains("mesh")) rEdits_.mesh = readMeshChoice(rJson_.at("mesh"));
	if(rJson_.contains("texture")) rEdits_.texture = readTextureChoice(rJson_.at("texture"));
}

/*
The external field values of an entity (Entity::externals) as the level file has them: {"<behaviour>": {"<field>": value}}, a number, bool or string as itself, a vec3 as [x, y, z], an entity as its index in the level (Level::editIndexOf; null if it is not
one of the level's or no level is given) and a prefab as its name (null: none). A value of a declared field is written from its type; one the file had that no declared field took is written back from Entity::externalsKept.
*/
nlohmann::ordered_json writeExternals(const Entity& rEntity_, const Level* pLevel_)
{
	nlohmann::ordered_json json = nlohmann::ordered_json::object();
	for(const auto& [behaviorName, rValues] : rEntity_.externals)
	{
		auto behavior = Globals::Game::entityBehaviorTypes.nameToId.find(behaviorName);
		if(behavior == Globals::Game::entityBehaviorTypes.nameToId.end()) continue;
		nlohmann::ordered_json fields = nlohmann::ordered_json::object();
		for(const ExternalField& rField : Globals::Game::entityBehaviorTypes[behavior->second].fields)
		{
			auto found = rValues.find(rField.name);
			if(found == rValues.end()) continue;
			fields[rField.name] = writeExternalValue(rField.type, found->second, pLevel_);
		}
		if(!fields.empty()) json[behaviorName] = fields;
	}

	/* What no declared field took when the file was read (Entity::externalsKept) is written back as it was, unless a declared value of the same field was written above. */
	for(const auto& [behaviorName, rValues] : rEntity_.externalsKept)
	{
		for(const auto& [fieldName, rText] : rValues)
		{
			if(json.contains(behaviorName) && json[behaviorName].contains(fieldName)) continue;
			json[behaviorName][fieldName] = nlohmann::ordered_json::parse(rText);
		}
	}
	return json;
}

/* The values of writeExternals onto an entity. An entity value is left as entityIndex (Level::resolveExternalEntities turns it into the entity once all of them exist). What does not fit (an undeclared behaviour or field, a wrong type, an unknown prefab) is reported on stderr and kept as JSON text (Entity::externalsKept) so a save writes it back. */
void readExternals(const Json& rJson_, Entity& rEntity_, const std::string& rLevel_)
{
	for(const auto& [behaviorName, rFields] : rJson_.items())
	{
		auto behavior = Globals::Game::entityBehaviorTypes.nameToId.find(behaviorName);
		if(behavior == Globals::Game::entityBehaviorTypes.nameToId.end())
		{
			fprintf(stderr, "Level '%s': an edit sets fields of the unknown behaviour '%s'; kept as they are.\n", rLevel_.c_str(), behaviorName.c_str());
			for(const auto& [fieldName, rJson] : rFields.items()) rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
			continue;
		}
		const std::vector<ExternalField>& rDeclared = Globals::Game::entityBehaviorTypes[behavior->second].fields;
		for(const auto& [fieldName, rJson] : rFields.items())
		{
			auto field = std::find_if(rDeclared.begin(), rDeclared.end(), [&](const ExternalField& rField_) { return rField_.name == fieldName; });
			if(field == rDeclared.end())
			{
				fprintf(stderr, "Level '%s': an edit sets the undeclared field '%s' of behaviour '%s'; kept as it is.\n", rLevel_.c_str(), fieldName.c_str(), behaviorName.c_str());
				rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
				continue;
			}
			ExternalValue value;
			try
			{
				value = readExternalValue(field->type, rJson);
			}
			catch(const std::exception& rError)
			{
				fprintf(stderr, "Level '%s': the field '%s' of behaviour '%s' does not fit: %s; kept as it is.\n", rLevel_.c_str(), fieldName.c_str(), behaviorName.c_str(), rError.what());
				rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
				continue;
			}
			rEntity_.externals[behaviorName][fieldName] = value;
		}
	}
}

/* A prefab's file in the level's Source Game Level under a root ("Assets/Levels/": the game's copy; the source tree's Levels/: what the next build copies back), by its pool name ("SADX/SPRING", "Level/<level>/<name>"). */
std::string sourcePrefabFile(const std::string& rRoot_, const std::string& rLevel_, const std::string& rPoolName_)
{
	return rRoot_ + rLevel_ + "/SourceGamePrefabs/" + rPoolName_ + "/prefab.json";
}

/* The properties an "added" entry (and a HubWorld entity's "state") gives an entity beyond its prefab and position: the whole behaviour list, the shapes, model, texture and external values, whichever the entry has. */
void applyEntityState(PoolId id, const Json& rAdded, const std::string& name_)
{
	/* An added entity keeps its whole list: there is no source to take a difference from. */
	if(rAdded.contains("behaviors"))
	{
		std::vector<PoolId> behaviorIds;
		for(const Json& rName : rAdded.at("behaviors"))
		{
			PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
			if(behaviorId != UninitializedPoolId) behaviorIds.push_back(behaviorId);
		}
		setEntityBehaviors(id, behaviorIds);
	}
	if(rAdded.contains("shapes"))
	{
		std::vector<Physics::CollisionShape> shapes;
		for(const Json& rShape : rAdded.at("shapes")) shapes.push_back(readShape(rShape, name_));
		setEntityShapes(id, shapes);
	}
	if(rAdded.contains("mesh")) applyMeshChoice(id, readMeshChoice(rAdded.at("mesh")), name_);
	if(rAdded.contains("texture")) applyTextureChoice(id, readTextureChoice(rAdded.at("texture")), name_);
	if(rAdded.contains("externals")) readExternals(rAdded.at("externals"), getEntity(id), name_);
}

/* One entry of an "added" list (see addEditedEntities): the entity of its prefab at its position with the properties the entry gives (behaviours, bound area, rotation, scale, model, texture, external values), or UninitializedPoolId, reported on stderr, if the prefab is unknown or it can't be made. */
PoolId makeAddedEntity(const Json& rAdded, const std::string& name_, PoolId shaderId_, PoolId levelId_)
{
	std::string prefabName = rAdded.at("prefab").get<std::string>();
	auto prefab = Globals::Game::prefabs.nameToId.find(prefabName);
	if(prefab == Globals::Game::prefabs.nameToId.end())
	{
		fprintf(stderr, "Level '%s': an edit adds the unknown prefab '%s'.\n", name_.c_str(), prefabName.c_str());
		return UninitializedPoolId;
	}
	SpawnParams params;
	params.position = readPosition(rAdded.at("position"));
	params.what = "(level file edit)";
	params.levelId = levelId_;
	PoolId id = spawnPrefab(prefab->second, shaderId_, params);
	if(id == UninitializedPoolId) return id;

	if(rAdded.contains("rotation")) getEntity(id).setRotation(readRotation(rAdded.at("rotation")));
	if(rAdded.contains("scale")) getEntity(id).setScale(readPosition(rAdded.at("scale")));
	applyEntityState(id, rAdded, name_);
	return id;
}

/* "added": [{"prefab", "position", ...}], the entities of a prefab the file adds to the level, after the ones it has; they are owned by the level. With keepIndices_ (the source set) an entry that can't be made leaves a gone entity in entityIds, so the entities after it keep the index the file counts them by. */
void addEditedEntities(Level& rLevel_, const Json& rEdits, const std::string& name_, PoolId shaderId_, bool keepIndices_)
{
	if(!rEdits.contains("added")) return;
	uint32_t addedIndex = 0;
	for(const Json& rAdded : rEdits.at("added"))
	{
		PoolId id = makeAddedEntity(rAdded, name_, shaderId_, rLevel_.selfId);
		EntityRecipe recipe;
		if(id != UninitializedPoolId)
		{
			recipe.kind = EntityRecipe::Kind::Added;
			recipe.shaderId = shaderId_;
			recipe.fromSource = keepIndices_;
			recipe.addedIndex = addedIndex;
		}
		addedIndex++;
		if(keepIndices_ || id != UninitializedPoolId) rLevel_.addBuilt(id, recipe);
	}
}

/*
Applies one set of edits (the "edits" of the level's file, see saveLevelEdits, or its "source", the same format without "prefabs") to the level just made: the moved ones are put at their position, the added ones are made from their prefab and
owned by the level. An edit that does not fit (an index past the entities, an unknown prefab) is reported on stderr and skipped.
With only_ >= 0 only the per-entity edits of that index are applied (the prefab-wide ones and the added entities are not, and the entity references are not resolved: the caller does): what resetLevel does to an entity it made again.
*/
void applyEditSet(Level& rLevel_, const Json& rEdits, const std::string& name_, PoolId shaderId_, bool asSource_, int64_t only_ = -1)
{
	/* The source set (Save Source Game Level) makes its added entities first, so its per-entity edits can name them (their index follows the level's own entities); the edits set names only entities that exist before it. */
	if(asSource_ && only_ < 0) addEditedEntities(rLevel_, rEdits, name_, shaderId_, true);

	/* "prefabs": {"<prefab>": {"added": [behaviour names], "removed": [...], "shapesAdded": [shapes], "shapesRemoved": [...], "mesh": choice, "texture": choice}}, the behaviours and bound area shapes given to or taken from every entity of a prefab and the model and texture all of them get; applied first, so the per-entity ones below are differences from the result. */
	if(only_ < 0 && rEdits.contains("prefabs"))
	{
		for(const auto& [prefabName, rLists] : rEdits.at("prefabs").items())
		{
			auto prefab = Globals::Game::prefabs.nameToId.find(prefabName);
			if(prefab == Globals::Game::prefabs.nameToId.end())
			{
				fprintf(stderr, "Level '%s': an edit changes the unknown prefab '%s'.\n", name_.c_str(), prefabName.c_str());
				continue;
			}
			for(const Json& rName : rLists.value("added", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) addPrefabBehavior(prefab->second, behaviorId, PrefabScope::Level, rLevel_.selfId);
			}
			for(const Json& rName : rLists.value("removed", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) removePrefabBehavior(prefab->second, behaviorId, PrefabScope::Level, rLevel_.selfId);
			}
			for(const Json& rShape : rLists.value("shapesAdded", Json::array())) addPrefabShape(prefab->second, readShape(rShape, name_), PrefabScope::Level, rLevel_.selfId);
			for(const Json& rShape : rLists.value("shapesRemoved", Json::array())) removePrefabShape(prefab->second, readShape(rShape, name_), PrefabScope::Level, rLevel_.selfId);
			/* An External prefab's model and texture are saved with the prefab (ExternalPrefabs.h), not here. */
			bool ownFile = Globals::Game::prefabs[prefab->second].kind == PrefabKind::External;
			if(!ownFile && rLists.contains("mesh"))
			{
				MeshChoice choice = readMeshChoice(rLists.at("mesh"));
				if(setPrefabMesh(prefab->second, choice, PrefabScope::Level, rLevel_.selfId) != 0) fprintf(stderr, "Level '%s': the model '%s' of prefab '%s' does not fit some of its entities.\n", name_.c_str(), choice.name.c_str(), prefabName.c_str());
			}
			if(!ownFile && rLists.contains("texture"))
			{
				TextureChoice choice = readTextureChoice(rLists.at("texture"));
				if(setPrefabTexture(prefab->second, choice, PrefabScope::Level, rLevel_.selfId) != 0) fprintf(stderr, "Level '%s': the texture '%s' of prefab '%s' does not fit some of its entities.\n", name_.c_str(), choice.name.c_str(), prefabName.c_str());
			}
		}
	}

	/* "behaviors": [{"index", "added", "removed"}], what one of the level's own entities has more or less than the game's data and the prefab edits give it. */
	if(rEdits.contains("behaviors"))
	{
		for(const Json& rEntry : rEdits.at("behaviors"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the behaviours of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			PoolId entityId = rLevel_.entityIds[index];
			for(const Json& rName : rEntry.value("removed", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) removeEntityBehavior(entityId, behaviorId);
			}
			for(const Json& rName : rEntry.value("added", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) giveEntityBehavior(entityId, behaviorId);
			}
		}
	}
	/* "shapes": [{"index", "shapes": [...]}], the whole bound area of one of the level's own entities when it differs from the game's data plus the prefab edits. */
	if(rEdits.contains("shapes"))
	{
		for(const Json& rEntry : rEdits.at("shapes"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the bound area of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			std::vector<Physics::CollisionShape> shapes;
			for(const Json& rShape : rEntry.at("shapes")) shapes.push_back(readShape(rShape, name_));
			setEntityShapes(rLevel_.entityIds[index], shapes);
		}
	}
	/* "meshes": [{"index", "mesh": choice}], the model of one of the level's own entities when it differs from its prefab's; applied before the textures. */
	if(rEdits.contains("meshes"))
	{
		for(const Json& rEntry : rEdits.at("meshes"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the model of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			applyMeshChoice(rLevel_.entityIds[index], readMeshChoice(rEntry.at("mesh")), name_);
		}
	}
	/* "textures": [{"index", "texture": choice}], the texture of one of the level's own entities when it differs from its prefab's. */
	if(rEdits.contains("textures"))
	{
		for(const Json& rEntry : rEdits.at("textures"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the texture of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			applyTextureChoice(rLevel_.entityIds[index], readTextureChoice(rEntry.at("texture")), name_);
		}
	}
	if(rEdits.contains("moved"))
	{
		for(const Json& rMoved : rEdits.at("moved"))
		{
			uint32_t index = rMoved.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit moves entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			getEntity(rLevel_.entityIds[index]).setPosition(readPosition(rMoved.at("position")));
		}
	}
	/* "transforms": [{"index", "rotation", "scale"}], the rotation and scale of one of the level's own entities when either differs from the game's data. */
	if(rEdits.contains("transforms"))
	{
		for(const Json& rEntry : rEdits.at("transforms"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit turns or scales entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			Entity& rEntity = getEntity(rLevel_.entityIds[index]);
			rEntity.setRotation(readRotation(rEntry.at("rotation")));
			rEntity.setScale(readPosition(rEntry.at("scale")));
		}
	}
	/* "externals": [{"index", "values"}], the external field values of one of the level's own entities (writeExternals); an added entity keeps its own under "added". Entity references are resolved at the end, once the added entities exist. */
	if(rEdits.contains("externals"))
	{
		for(const Json& rEntry : rEdits.at("externals"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(only_ >= 0 && index != static_cast<uint32_t>(only_)) continue;
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit sets fields of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			readExternals(rEntry.at("values"), getEntity(rLevel_.entityIds[index]), name_);
		}
	}
	if(!asSource_ && only_ < 0) addEditedEntities(rLevel_, rEdits, name_, shaderId_, false);
	if(only_ < 0) rLevel_.resolveExternalEntities();
}

/*
Applies the "source" (if any) and then the "edits" (if any) of the level's file to the level just made. The source is the level as saved by File > Save Source Game Level: applied first, then the level's entities are marked as the source (markSourceEntities), so Restore Source Game Level and the edits' differences are from it.
The file is read again (not the recipe's copy): a saved edit is in the file the next time the level is loaded.
*/
/*
"cameras" (top level of the level file, written by File > Save and Save Source Game Level): [{"name", "position", "front", "yaw", "pitch", "fov", "near", "far", "enabled", "main", "renderTarget": {"width", "height"}}], the camera entities the editor added to the level. Each is made after
the edits as a camera entity (addCamera) that the level owns, so it is removed with the level and by Restore Source Game Level. "main" makes it the main camera; "renderTarget" makes a new render target of that size for it (absent: the screen). A broken entry is reported on stderr and skipped.
*/
PoolId makeLevelCamera(const Json& rEntry_, PoolId levelId_)
{
	glm::vec3 position = readPosition(rEntry_.at("position"));
	PoolId id = addCamera(rEntry_.value("name", std::string("Camera")).c_str(), position);
	getEntity(id).levelId = levelId_;
	PoolId cameraId = getEntity(id).cameraId;
	Camera& rCamera = Globals::Game::cameras[cameraId];
	if(rEntry_.contains("front")) rCamera.front = readPosition(rEntry_.at("front"));
	rCamera.yaw = rEntry_.value("yaw", rCamera.yaw);
	rCamera.pitch = rEntry_.value("pitch", rCamera.pitch);
	rCamera.fov = rEntry_.value("fov", rCamera.fov);
	rCamera.nearPlane = rEntry_.value("near", rCamera.nearPlane);
	rCamera.farPlane = rEntry_.value("far", rCamera.farPlane);
	rCamera.enabled = rEntry_.value("enabled", rCamera.enabled);
	if(rEntry_.contains("renderTarget"))
	{
		const Json& rTarget = rEntry_.at("renderTarget");
		rCamera.setRenderTarget(addRenderTarget(rTarget.at("width").get<uint32_t>(), rTarget.at("height").get<uint32_t>()));
	}
	rCamera.refreshProjection();
	rCamera.set(position);
	if(rEntry_.value("main", false)) Globals::Game::mainCamera = cameraId;
	return id;
}

void addLevelCameras(Level& rLevel_, const Json& rCameras_, const std::string& name_)
{
	uint32_t listIndex = 0;
	for(const Json& rEntry : rCameras_)
	{
		EntityRecipe recipe;
		recipe.kind = EntityRecipe::Kind::Camera;
		recipe.addedIndex = listIndex++;
		try
		{
			rLevel_.addBuilt(makeLevelCamera(rEntry, rLevel_.selfId), recipe);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Level '%s': a camera of the file can't be made: %s\n", name_.c_str(), rError.what());
		}
	}
}

/*
"entities" (top level of the level file, written by File > Save and Save Source Game Level): [{"name", "position", "rotation", "scale", "behaviors": [names], "shapes": [shapes]}], the plain entities the editor added to the level (Add empty entity: no prefab, no model, no camera). Each is made after the edits (addEntity) and owned by the level, so it is removed with the level
and by Restore Source Game Level. "rotation" (nine numbers) and "scale" are written only when they are not the identity. A broken entry is reported on stderr and skipped.
*/
PoolId makeLevelEntity(const Json& rEntry_, const std::string& name_, PoolId levelId_)
{
	PoolId id = addEntity(rEntry_.value("name", std::string("Entity")).c_str(), readPosition(rEntry_.at("position")));
	getEntity(id).levelId = levelId_;
	std::vector<PoolId> behaviorIds;
	for(const Json& rName : rEntry_.value("behaviors", Json::array()))
	{
		PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
		if(behaviorId != UninitializedPoolId) behaviorIds.push_back(behaviorId);
	}
	if(!behaviorIds.empty()) setEntityBehaviors(id, behaviorIds);
	std::vector<Physics::CollisionShape> shapes;
	for(const Json& rShape : rEntry_.value("shapes", Json::array())) shapes.push_back(readShape(rShape, name_));
	if(!shapes.empty()) setEntityShapes(id, shapes);
	if(rEntry_.contains("rotation")) getEntity(id).setRotation(readRotation(rEntry_.at("rotation")));
	if(rEntry_.contains("scale")) getEntity(id).setScale(readPosition(rEntry_.at("scale")));
	return id;
}

void addLevelEntities(Level& rLevel_, const Json& rEntities_, const std::string& name_)
{
	uint32_t listIndex = 0;
	for(const Json& rEntry : rEntities_)
	{
		EntityRecipe recipe;
		recipe.kind = EntityRecipe::Kind::Plain;
		recipe.addedIndex = listIndex++;
		try
		{
			rLevel_.addBuilt(makeLevelEntity(rEntry, name_, rLevel_.selfId), recipe);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Level '%s': an entity of the file can't be made: %s\n", name_.c_str(), rError.what());
		}
	}
}

/*
"removed" (top level of the level file, written by File > Save and Save Source Game Level): [index], the level's own entities (the game's data and the file's "source", by their index in Level::entityIds) the editor deleted. Each is removed after the edits are applied and keeps its slot, so the other indices stay as they are.
An index that is not a source entity is ignored. Restore Source Game Level does not bring them back; taking the index out of the list (or the key) and loading the level again does.
*/
void removeSourceEntities(Level& rLevel_, const Json& rRemoved_)
{
	for(const Json& rIndex : rRemoved_) rLevel_.removeSourceEntity(rIndex.get<uint32_t>());
}

void applyEdits(Level& rLevel_, const std::string& name_, PoolId shaderId_)
{
	Json file;
	std::ifstream stream(levelFilePath(Paths::Levels, name_));
	if(stream) file = Json::parse(stream);
	if(file.contains("source")) applyEditSet(rLevel_, file.at("source"), name_, shaderId_, true);
	rLevel_.markSourceEntities();
	if(file.contains("edits")) applyEditSet(rLevel_, file.at("edits"), name_, shaderId_, false);
	if(file.contains("cameras")) addLevelCameras(rLevel_, file.at("cameras"), name_);
	if(file.contains("entities")) addLevelEntities(rLevel_, file.at("entities"), name_);
	if(file.contains("removed")) removeSourceEntities(rLevel_, file.at("removed"));
}
}

namespace
{
/*
Fills a level that addLevel made (or that resetLevel emptied): its name, External prefabs, the instance, collision and placed entities from the recipe, the file's edits, and last its place in the HubWorld. Throws what the steps throw; the caller decides what a failed level becomes.
With keepInstance_ the level still has its instance, collision and External assets (resetLevel destroyed only its entities): those steps are skipped and the place in the HubWorld is applied from the unmoved state the new entities are in. Not for a level that has none yet.
*/
void buildLevel(PoolId id_, const std::string& name_, Recipe& rRecipe_, bool keepInstance_ = false)
{
	Level& rLevel = getLevel(id_);
	rLevel.name = name_;
#ifdef EDITOR
	/* Before the edits: they may name the level's own External prefabs. */
	if(!keepInstance_) loadExternalPrefabs(ExternalScope::Level, name_);
#endif
	/* The entities are made from the game's data and the level's Source Game Level prefab sets alone (Level::sourceBehaviors); the file's prefab edits are applied after them (applyEdits). The level's own sets start empty: its slot's were dropped when the level before it was destroyed. */
	loadSourcePrefabs(name_, id_);
	if(rRecipe_.gltfLevel != nullptr)
	{
		if(!keepInstance_)
		{
			rLevel.setGLTFGPUInstance(rRecipe_.shaderId, *rRecipe_.gltfLevel);
			rLevel.setGLTFCollision(*rRecipe_.gltfLevel);
		}
	}
	else buildGameLevel(rLevel, rRecipe_, keepInstance_);
	applyEdits(rLevel, name_, rRecipe_.shaderId);

	/* The level was made and edited in its own coordinates; the HubWorld's place for it (HubWorld.json) moves all of it, so its file's edits stay local (saveLevelEdits writes them back unmoved). */
	glm::vec3 placedPosition = rLevel.hubWorldPosition;
	glm::vec3 placedRotation = rLevel.hubWorldRotation;
	bool wasStale = rLevel.collisionStale;
	if(keepInstance_)
	{
		/* The new entities are in local coordinates and the collision is already placed: the HubWorld place is taken off the level and put on again without moving the collision. */
		rLevel.hubWorldPosition = glm::vec3(0.0f);
		rLevel.hubWorldRotation = glm::vec3(0.0f);
	}
	if(const HubWorldLevel* pPlace = Globals::Game::hubWorld.loadedAsHub ? Globals::Game::hubWorld.find(name_) : nullptr) rLevel.setHubWorldTransform(pPlace->position, pPlace->rotation, !keepInstance_);
	if(keepInstance_)
	{
		/* The collision is at the place it had before the reset; if the editor moved the level's place while playing (or was dragging it) it is put on the place the HubWorld file gives now. */
		rLevel.collisionStale = false;
		if(wasStale || placedPosition != rLevel.hubWorldPosition || placedRotation != rLevel.hubWorldRotation)
		{
			rLevel.collisionStale = true;
			rLevel.commitHubWorldCollision();
		}
	}
	rLevel.captureBaselines();
}
}

PoolId loadLevel(const std::string& name_)
{
	bool known = recipes.contains(name_);
	Recipe& rRecipe = recipes[name_];
	if(!known)
	{
		try
		{
			readRecipe(name_, rRecipe);
		}
		catch(const std::exception& rError)
		{
			recipes.erase(name_);
			throw std::runtime_error(std::format("Level '{}': {}", name_, rError.what()));
		}
	}

#ifdef EDITOR
	/* The editor opens a level: register the external assets (editor-wide and this level's folder) again before the level exists. */
	scanExternalAssets(name_);
#endif

	PoolId id = addLevel(name_.c_str(), rRecipe.position);
	try
	{
		buildLevel(id, name_, rRecipe);
	}
	catch(...)
	{
		Globals::Game::levels.remove(id);
		throw;
	}
	Globals::Game::spatialIndex.rebuild();
	return id;
}

/* Removes the entities the HubWorld owns (the next load makes them again from HubWorld::entities). */
static void removeHubWorldEntities()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	for(PoolId id : rHubWorld.entityIds)
	{
		if(!entityExists(id)) continue;

		/* A camera's render target is a pool object of its own that removing the camera does not free; makeLevelCamera made it for the entity. */
		if(getEntity(id).cameraId != UninitializedPoolId)
		{
			PoolId targetId = Globals::Game::cameras[getEntity(id).cameraId].renderTargetId;
			Pool<Graphics::RenderTarget>& rTargets = Globals::Game::renderTargets;
			if(targetId != UninitializedPoolId && targetId.id < rTargets.alive.size() && rTargets.alive[targetId.id] && rTargets.generation[targetId.id] == targetId.generation) rTargets.remove(targetId);
		}
		Globals::Game::entities.remove(id);
	}
	rHubWorld.entityIds.clear();
}

/* Makes the entities of HubWorld.json: a plain entity, or an entity of its prefab, at its transform in HubWorld coordinates and with its saved state (applyEntityState), owned by the HubWorld (no level). One that can't be made is on stderr and skipped. */
static void buildHubWorldEntities()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	for(const HubWorldEntity& rEntry : rHubWorld.entities)
	{
		PoolId id = UninitializedPoolId;
		Json state;
		try
		{
			if(!rEntry.state.empty()) state = Json::parse(rEntry.state);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "HubWorld: entity '%s': its state is not read: %s\n", rEntry.name.c_str(), rError.what());
			state = Json();
		}

		/* A camera entity: made like a level's camera ("camera" is that entry's format without the name and position). */
		if(state.is_object() && state.contains("camera"))
		{
			try
			{
				Json cameraEntry = state.at("camera");
				cameraEntry["name"] = rEntry.name;
				cameraEntry["position"] = Json::array({rEntry.position.x, rEntry.position.y, rEntry.position.z});
				id = makeLevelCamera(cameraEntry, UninitializedPoolId);
			}
			catch(const std::exception& rError)
			{
				fprintf(stderr, "HubWorld: camera '%s' can't be made: %s\n", rEntry.name.c_str(), rError.what());
				continue;
			}
			getEntity(id).hubWorldOwned = true;
			rHubWorld.entityIds.push_back(id);
			continue;
		}
		if(rEntry.prefab.empty()) id = addEntity(rEntry.name.c_str(), rEntry.position);
		else
		{
			auto found = Globals::Game::prefabs.nameToId.find(rEntry.prefab);
			if(found == Globals::Game::prefabs.nameToId.end())
			{
				fprintf(stderr, "HubWorld: entity '%s': there is no prefab named '%s'\n", rEntry.name.c_str(), rEntry.prefab.c_str());
				continue;
			}
			SpawnParams params;
			params.position = rEntry.position;
			params.rotation = glm::mat4(rEntry.rotation);
			params.name = rEntry.name;
			params.what = "(HubWorld)";
			id = spawnPrefab(found->second, Graphics::shaders.nameToId.at(gameShaderName(Globals::Game::prefabs[found->second].game)), params);
			if(id == UninitializedPoolId)
			{
				fprintf(stderr, "HubWorld: entity '%s' of '%s' could not be made\n", rEntry.name.c_str(), rEntry.prefab.c_str());
				continue;
			}
		}
		Entity& rEntity = getEntity(id);
		rEntity.setRotation(glm::mat4(rEntry.rotation));
		rEntity.setScale(rEntry.scale);
		rEntity.hubWorldOwned = true;
		rHubWorld.entityIds.push_back(id);
		if(!state.is_object()) continue;
		try
		{
			applyEntityState(id, state, "HubWorld");
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "HubWorld: entity '%s': its state is not applied: %s\n", rEntry.name.c_str(), rError.what());
		}
	}
}

#ifdef EDITOR
void syncHubWorldEntities()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	rHubWorld.entities.clear();
	for(PoolId id : rHubWorld.entityIds)
	{
		if(!entityExists(id)) continue;
		const Entity& rEntity = getEntity(id);
		HubWorldEntity entry;
		entry.name = Globals::Game::entities.names[id.id];
		if(rEntity.prefabId != UninitializedPoolId) entry.prefab = Globals::Game::prefabs.names[rEntity.prefabId.id];
		entry.position = rEntity.position;
		entry.rotation = glm::mat3(rEntity.rotation);
		entry.scale = rEntity.scale;

		/* A camera entity keeps only its camera (the format of a level file's "cameras" entry, without the name and position). */
		if(rEntity.cameraId != UninitializedPoolId)
		{
			const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
			nlohmann::ordered_json camera = {{"front", nlohmann::ordered_json::array({rCamera.front.x, rCamera.front.y, rCamera.front.z})}, {"yaw", rCamera.yaw}, {"pitch", rCamera.pitch}, {"fov", rCamera.fov}, {"near", rCamera.nearPlane}, {"far", rCamera.farPlane}, {"enabled", rCamera.enabled}};
			if(Globals::Game::mainCamera == rEntity.cameraId) camera["main"] = true;
			if(const Graphics::RenderTarget* pTarget = rCamera.renderTarget()) camera["renderTarget"] = {{"width", pTarget->width}, {"height", pTarget->height}};
			entry.state = nlohmann::ordered_json{{"camera", camera}}.dump();
			rHubWorld.entities.push_back(entry);
			continue;
		}

		/* The entity's whole behaviour list and shapes, its field values, and its model and texture when they differ from what the prefab gives (a plain entity has none): an "added" entry's format, read by applyEntityState. */
		nlohmann::ordered_json state = nlohmann::ordered_json::object();
		nlohmann::ordered_json names = nlohmann::ordered_json::array();
		if(rEntity.behaviorSetId != UninitializedPoolId)
		{
			for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) names.push_back(Globals::Game::entityBehaviorTypes.names[behaviorId.id]);
		}
		state["behaviors"] = names;
		nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
		for(const Physics::CollisionShape& rShape : entityShapes(id)) shapes.push_back(writeShape(rShape));
		state["shapes"] = shapes;
		bool hasPrefab = rEntity.prefabId != UninitializedPoolId;
		if(rEntity.meshChoice != (hasPrefab ? prefabMesh(Globals::Game::prefabs[rEntity.prefabId], UninitializedPoolId) : MeshChoice{})) state["mesh"] = writeMeshChoice(rEntity.meshChoice);
		if(rEntity.textureChoice != (hasPrefab ? prefabTexture(Globals::Game::prefabs[rEntity.prefabId], UninitializedPoolId) : TextureChoice{})) state["texture"] = writeTextureChoice(rEntity.textureChoice);
		nlohmann::ordered_json values = writeExternals(rEntity, nullptr);
		if(!values.empty()) state["externals"] = values;
		entry.state = state.dump();
		rHubWorld.entities.push_back(entry);
	}
}
#endif

/* Removes the levels the last load made beside the active one, and the HubWorld's own entities. */
static void removeHubWorldLoaded()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	removeHubWorldEntities();
	for(PoolId id : rHubWorld.loaded)
	{
		if(id.id < Globals::Game::levels.objects.size() && Globals::Game::levels.alive[id.id] && Globals::Game::levels.generation[id.id] == id.generation) Globals::Game::levels.remove(id);
	}
	rHubWorld.loaded.clear();
}

PoolId loadLevelAlone(const std::string& name_)
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	removeHubWorldLoaded();
	rHubWorld.loadedAsHub = false;
	PoolId id = loadLevel(name_);
	rHubWorld.active = name_;
	return id;
}

PoolId loadHubWorld(const std::string& activeName_)
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	if(rHubWorld.levels.empty()) throw std::runtime_error("HubWorld.json has no levels");
	const std::string& rActive = activeName_.empty() ? rHubWorld.levels.front().name : activeName_;
	removeHubWorldLoaded();

	/* loadedAsHub is set before the active level is made: loadLevel gives a level its HubWorld place only then. */
	rHubWorld.loadedAsHub = true;
	PoolId activeId = loadLevel(rActive);
	rHubWorld.active = rActive;
	for(const HubWorldLevel& rEntry : rHubWorld.levels)
	{
		if(rEntry.name == rActive) continue;
		try
		{
			rHubWorld.loaded.push_back(loadLevel(rEntry.name));
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "HubWorld: %s\n", rError.what());
		}
	}
	buildHubWorldEntities();
	Globals::Game::spatialIndex.rebuild();
	return activeId;
}

namespace
{
/*
resetLevel's way of leaving the entities that did not change alone: of the entities the build made (Level::built), the ones that are not as the build left them (Level::entityChanged: moved, a script changed their state, destroyed, ...) are removed and made again from their recipe
(EntityRecipe) with the file's edits for their index applied, at the HubWorld's place; the entities of the level that the build did not make (the editor's) are removed. An entity that holds a reference to a changed one (an external field naming it) is made again too, since the reference would name
the old entity, and so on. Entities that did not change keep everything, the Lua state of their behaviours included (no setup runs again), which is the point: it is the cost of making all of them that the reset is trying to avoid.
A level the editor's Play kept (Editor::keptLevels) gets, for the entities made again, the state the editor had at Play, and the entities the editor added are made again (Editor::restoreKeptEntities); the entities left alone were taken as baseline at Play.
Returns false, with nothing changed, when the level has to be rebuilt whole: an entity that can't be made alone is changed (no recipe), the entities are not where the build put them, or (the editor's Play) an entity the file's edits added, or a camera of the file, has to be made again: its kept state is not
kept by place in the file.
*/
bool resetEntitiesInPlace(PoolId levelId_, const std::string& name_, uint32_t& rKept_, uint32_t& rRemade_, uint32_t& rRemoved_)
{
	Level& rLevel = getLevel(levelId_);
	if(rLevel.entityIds.size() < rLevel.builtCount) return false;
	for(uint32_t i = 0; i < rLevel.builtCount; i++)
	{
		PoolId id = rLevel.entityIds[i];
		if(id == UninitializedPoolId) continue;
		auto found = rLevel.built.find(Level::builtKey(id));
		if(found == rLevel.built.end() || found->second.index != i) return false;
	}

	/* Which of the build's entities to make again, by index: the changed ones, then the ones referring to them. */
	std::vector<char> remake(rLevel.builtCount, 0);
	for(uint32_t i = 0; i < rLevel.builtCount; i++)
	{
		PoolId id = rLevel.entityIds[i];
		if(id == UninitializedPoolId || rLevel.isRemoved(i) || !rLevel.entityChanged(id)) continue;
		if(rLevel.built.at(Level::builtKey(id)).recipe.kind == EntityRecipe::Kind::None) return false;
		remake[i] = 1;
	}
	for(bool grew = true; grew;)
	{
		grew = false;
		for(uint32_t i = 0; i < rLevel.builtCount; i++)
		{
			PoolId id = rLevel.entityIds[i];
			if(remake[i] || id == UninitializedPoolId || !entityExists(id)) continue;
			bool refers = false;
			for(const auto& [behaviorName, rValues] : getEntity(id).externals)
			{
				for(const auto& [fieldName, rValue] : rValues)
				{
					if(rValue.entity == UninitializedPoolId) continue;
					for(uint32_t j = 0; j < rLevel.builtCount && !refers; j++)
					{
						refers = remake[j] != 0 && rLevel.entityIds[j] == rValue.entity;
					}
				}
			}
			if(!refers) continue;
			if(rLevel.built.at(Level::builtKey(id)).recipe.kind == EntityRecipe::Kind::None) return false;
			remake[i] = 1;
			grew = true;
		}
	}

#ifdef EDITOR
	if(editor.hasKeptLevel(name_))
	{
		for(uint32_t i = rLevel.sourceEntityCount; i < rLevel.builtCount; i++)
		{
			if(remake[i]) return false;
		}
	}
#endif

	/* What the editor added after the build goes (its own restore puts it back: Editor::restoreKeptEntities). */
	for(uint32_t i = rLevel.builtCount; i < rLevel.entityIds.size(); i++)
	{
		rLevel.built.erase(Level::builtKey(rLevel.entityIds[i]));
		if(entityExists(rLevel.entityIds[i]))
		{
			Globals::Game::entities.remove(rLevel.entityIds[i]);
			rRemoved_++;
		}
	}
	rLevel.entityIds.resize(rLevel.builtCount);

	Json file;
	std::ifstream stream(levelFilePath(Paths::Levels, name_));
	if(stream) file = Json::parse(stream);

	/* First every entity is made, so the entity references of the file's edits (applied next) name the new ones. */
	std::vector<uint32_t> made;
	for(uint32_t i = 0; i < rLevel.builtCount; i++)
	{
		if(!remake[i])
		{
			if(rLevel.entityIds[i] != UninitializedPoolId) rKept_++;
			continue;
		}
		PoolId oldId = rLevel.entityIds[i];
		BuiltEntity built = std::move(rLevel.built.at(Level::builtKey(oldId)));
		rLevel.built.erase(Level::builtKey(oldId));
		PoolId oldCameraTarget = UninitializedPoolId;
		if(entityExists(oldId) && getEntity(oldId).cameraId != UninitializedPoolId && Globals::Game::cameras[getEntity(oldId).cameraId].renderTargetId == built.baseline.cameraRenderTargetId)
		{
			oldCameraTarget = built.baseline.cameraRenderTargetId;
		}
		if(entityExists(oldId)) Globals::Game::entities.remove(oldId);
		EntityRecipe& rRecipe = built.recipe;
		PoolId id = UninitializedPoolId;
		if(rRecipe.kind == EntityRecipe::Kind::Placed)
		{
			SpawnParams params = rRecipe.params;
			restoreGameSpawnParams(params, rRecipe);
			id = spawnPrefab(rRecipe.prefabId, rRecipe.shaderId, params);
		}
		else if(rRecipe.kind == EntityRecipe::Kind::Camera)
		{
			/* The old camera's render target, if it still has the one the build made for it, goes (makeLevelCamera makes a new one); the main camera is set again by the entry's "main". */
			if(oldCameraTarget != UninitializedPoolId) Globals::Game::renderTargets.remove(oldCameraTarget);
			if(file.contains("cameras") && file.at("cameras").size() > rRecipe.addedIndex)
			{
				try
				{
					id = makeLevelCamera(file.at("cameras")[rRecipe.addedIndex], rLevel.selfId);
				}
				catch(const std::exception& rError)
				{
					fprintf(stderr, "LevelReset: a camera of level %s not made again: %s\n", name_.c_str(), rError.what());
				}
			}
		}
		else if(rRecipe.kind == EntityRecipe::Kind::Plain)
		{
			if(file.contains("entities") && file.at("entities").size() > rRecipe.addedIndex)
			{
				try
				{
					id = makeLevelEntity(file.at("entities")[rRecipe.addedIndex], name_, rLevel.selfId);
				}
				catch(const std::exception& rError)
				{
					fprintf(stderr, "LevelReset: an entity of level %s not made again: %s\n", name_.c_str(), rError.what());
				}
			}
		}
		else
		{
			const char* pSet = rRecipe.fromSource ? "source" : "edits";
			if(file.contains(pSet) && file.at(pSet).contains("added") && file.at(pSet).at("added").size() > rRecipe.addedIndex)
			{
				id = makeAddedEntity(file.at(pSet).at("added")[rRecipe.addedIndex], name_, rRecipe.shaderId, rLevel.selfId);
			}
		}
		rLevel.entityIds[i] = id;
		if(id == UninitializedPoolId) continue;
		if(Scripting::ready()) Scripting::entityRemade(oldId, id);
		rLevel.built[Level::builtKey(id)] = std::move(built);
		made.push_back(i);
		rRemade_++;
	}

	/* The per-entity edits for the new ones, then the HubWorld's place (the build made and edited the level in its own coordinates and placed it last). */
	glm::mat4 place = rLevel.hubWorldMatrix();
	glm::mat4 placeRotation = glm::mat4(glm::mat3(place));
	for(uint32_t i : made)
	{
		PoolId id = rLevel.entityIds[i];
		const EntityRecipe& rMadeRecipe = rLevel.built.at(Level::builtKey(id)).recipe;
		PoolId shaderId = rMadeRecipe.shaderId;
		if((rMadeRecipe.kind != EntityRecipe::Kind::Camera && rMadeRecipe.kind != EntityRecipe::Kind::Plain) && file.contains("source")) applyEditSet(rLevel, file.at("source"), name_, shaderId, true, i);
		if((rMadeRecipe.kind != EntityRecipe::Kind::Camera && rMadeRecipe.kind != EntityRecipe::Kind::Plain) && file.contains("edits")) applyEditSet(rLevel, file.at("edits"), name_, shaderId, false, i);
		Entity& rEntity = getEntity(id);
		rEntity.setPosition(glm::vec3(place * glm::vec4(rEntity.position, 1.0f)));
		rEntity.setRotation(placeRotation * rEntity.rotation);
	}
#ifdef EDITOR
	editor.restoreKeptEntities(levelId_, made);
#endif
	rLevel.resolveExternalEntities();
	for(uint32_t i : made) rLevel.captureBaseline(rLevel.entityIds[i]);
	return true;
}
}

void resetLevel(PoolId levelId_)
{
	Level& rLevel = getLevel(levelId_);
	const std::string name = rLevel.name;
	Recipe& rRecipe = recipes.at(name);

	/* Only what changed is made again when it can be (resetEntitiesInPlace, which gives the editor's Play its kept state); otherwise the entities are all made again, below. */
	uint32_t kept = 0, remade = 0, removed = 0;
	if(resetEntitiesInPlace(levelId_, name, kept, remade, removed))
	{
		if(remade != 0 || removed != 0)
		{
			Globals::Game::spatialIndex.rebuild();
		}
		return;
	}
	/* Only the entities are rebuilt: the instance (the meshes and textures uploaded) and the collision stay, which was most of the cost of destroying the whole level (TODO, Level reset, step 5). What the editor changes in them while playing (the HubWorld place) is put back below; scripts change neither. */

	/* What each entity of the build was (by its place in the level and its recipe), to tell a script's kept handle which new entity took its place once the build is done. */
	struct BuiltIdentity
	{
		PoolId id;
		EntityRecipe::Kind kind;
		PoolId prefabId;
		bool fromSource;
		uint32_t addedIndex;
	};
	std::vector<BuiltIdentity> before;
	for(uint32_t i = 0; i < rLevel.builtCount && i < rLevel.entityIds.size(); i++)
	{
		auto found = rLevel.built.find(Level::builtKey(rLevel.entityIds[i]));
		if(found == rLevel.built.end() || found->second.index != i) before.push_back({UninitializedPoolId, EntityRecipe::Kind::None, UninitializedPoolId, false, 0});
		else before.push_back({rLevel.entityIds[i], found->second.recipe.kind, found->second.recipe.prefabId, found->second.recipe.fromSource, found->second.recipe.addedIndex});
	}
	rLevel.destroyEntities();
	try
	{
		buildLevel(levelId_, name, rRecipe, true);
	}
	catch(const std::exception& rError)
	{
		/* The level stays, without entities: scripts may hold its handle, and the player still has to be somewhere. */
		fprintf(stderr, "LevelReset: level %s not made again: %s\n", name.c_str(), rError.what());
		return;
	}
	/* A new entity takes the place of an old one only when it is the same one by the recipe (the same place in the level, kind and prefab, and the same entry of the file's lists): a file the editor changed may have moved the entities. */
	if(Scripting::ready())
	{
		for(uint32_t i = 0; i < before.size() && i < rLevel.builtCount && i < rLevel.entityIds.size(); i++)
		{
			PoolId newId = rLevel.entityIds[i];
			if(before[i].id == UninitializedPoolId || newId == UninitializedPoolId) continue;
			auto found = rLevel.built.find(Level::builtKey(newId));
			if(found == rLevel.built.end()) continue;
			const EntityRecipe& rNew = found->second.recipe;
			if(rNew.kind == before[i].kind && rNew.prefabId == before[i].prefabId && rNew.fromSource == before[i].fromSource && rNew.addedIndex == before[i].addedIndex) Scripting::entityRemade(before[i].id, newId);
		}
	}
#ifdef EDITOR
	editor.restoreKeptLevel(levelId_);
#endif
	Globals::Game::spatialIndex.rebuild();
}

void loadSourcePrefabs(const std::string& levelName_, PoolId levelId_)
{
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		PrefabEdits& rSource = Globals::Game::prefabs.objects[i].inLevels[levelId_.id].source;
		rSource = PrefabEdits{};
		const std::string& rName = Globals::Game::prefabs.names[i];
		std::string path = sourcePrefabFile(Paths::Levels, levelName_, rName);
		std::ifstream stream(path);
		if(!stream) continue;
		try
		{
			readPrefabEdits(rSource, Json::parse(stream), "Source Game Level " + rName);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Source Game Level prefab not read (%s): %s\n", path.c_str(), rError.what());
		}
	}
}

namespace
{
/* A prefab's Changed file under a root ("Assets/External/": the game's copy; the source tree's External/: what the next build copies back), by its pool name ("SADX/SPRING"; a key name has no character a path can't have). */
std::string changedPrefabFile(const std::string& rRoot_, const std::string& rPoolName_)
{
	return rRoot_ + "Prefabs/" + rPoolName_ + "/Changed/prefab.json";
}
}

void loadChangedPrefabs()
{
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i] || Globals::Game::prefabs.objects[i].kind == PrefabKind::External) continue;
		const std::string& rName = Globals::Game::prefabs.names[i];
		std::string path = changedPrefabFile(Paths::External, rName);
		std::ifstream stream(path);
		if(!stream) continue;
		try
		{
			readPrefabEdits(Globals::Game::prefabs.objects[i].changed, Json::parse(stream), "Changed " + rName);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Changed prefab not read (%s): %s\n", path.c_str(), rError.what());
		}
	}
}

#ifdef EDITOR
void saveChangedPrefab(PoolId prefabId_)
{
	namespace fs = std::filesystem;
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	const std::string& rName = Globals::Game::prefabs.names[prefabId_.id];
	bool empty = prefabEditsEmpty(rPrefab.changed, true);
	std::string text = empty ? std::string() : writePrefabEdits(rPrefab.changed, true).dump(2);
	for(const std::string& rRoot : {std::string(Paths::External), std::string(Paths::SourceExternal)})
	{
		fs::path path = changedPrefabFile(rRoot, rName);
		if(empty)
		{
			fs::remove(path);
			continue;
		}
		fs::create_directories(path.parent_path());
		std::ofstream out(path);
		if(!out) throw std::runtime_error(std::format("can't write {}.", path.string()));
		out << text;
	}
}

/* The "edits" object of the level as it is now, from its source (the game's data plus the level file's "source"): the differences only; an empty object when there are none. */
static nlohmann::ordered_json buildLevelEdits(const Level& rLevel)
{
	auto position = [](const glm::vec3& rPosition_) { return nlohmann::ordered_json::array({rPosition_.x, rPosition_.y, rPosition_.z}); };
	auto rotation = [](const glm::mat4& rRotation_)
	{
		glm::mat3 matrix(rRotation_);
		return nlohmann::ordered_json::array({matrix[0].x, matrix[0].y, matrix[0].z, matrix[1].x, matrix[1].y, matrix[1].z, matrix[2].x, matrix[2].y, matrix[2].z});
	};

	/* The level's entities are in world coordinates (Level::hubWorldMatrix is applied to them); the file keeps the level's own, so every position and rotation written goes back through the inverse of the level's place in the HubWorld. */
	glm::mat4 toLocal = glm::inverse(rLevel.hubWorldMatrix());
	auto localPoint = [&](const glm::vec3& rPoint_) { return glm::vec3(toLocal * glm::vec4(rPoint_, 1.0f)); };
	auto localRotation = [&](const glm::mat4& rRotation_) { return glm::mat4(glm::mat3(toLocal)) * rRotation_; };
	nlohmann::ordered_json moved = nlohmann::ordered_json::array();
	nlohmann::ordered_json transforms = nlohmann::ordered_json::array();
	nlohmann::ordered_json added = nlohmann::ordered_json::array();
	nlohmann::ordered_json externals = nlohmann::ordered_json::array();
	for(uint32_t i = 0; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(i < rLevel.sourceEntityCount)
		{
			if(rEntity.position != rLevel.sourcePositions[i]) moved.push_back({{"index", i}, {"position", position(localPoint(rEntity.position))}, {"source", position(localPoint(rLevel.sourcePositions[i]))}});
			if(rEntity.rotation != rLevel.sourceRotations[i] || rEntity.scale != rLevel.sourceScales[i]) transforms.push_back({{"index", i}, {"rotation", rotation(localRotation(rEntity.rotation))}, {"scale", position(rEntity.scale)}});
			nlohmann::ordered_json values = writeExternals(rEntity, &rLevel);
			if(!values.empty()) externals.push_back({{"index", i}, {"values", values}});
		}
		else if(rEntity.prefabId != UninitializedPoolId)
		{
			nlohmann::ordered_json names = nlohmann::ordered_json::array();
			if(rEntity.behaviorSetId != UninitializedPoolId)
			{
				for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) names.push_back(Globals::Game::entityBehaviorTypes.names[behaviorId.id]);
			}
			nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
			for(const Physics::CollisionShape& rShape : entityShapes(rEntity.selfId)) shapes.push_back(writeShape(rShape, &rLevel));
			added.push_back({{"prefab", Globals::Game::prefabs.names[rEntity.prefabId.id]}, {"position", position(localPoint(rEntity.position))}, {"behaviors", names}, {"shapes", shapes}});
			if(rEntity.rotation != glm::mat4(1.0f)) added.back()["rotation"] = rotation(localRotation(rEntity.rotation));
			if(rEntity.scale != glm::vec3(1.0f)) added.back()["scale"] = position(rEntity.scale);
			if(rEntity.meshChoice != prefabMesh(Globals::Game::prefabs[rEntity.prefabId], rLevel.selfId)) added.back()["mesh"] = writeMeshChoice(rEntity.meshChoice);
			if(rEntity.textureChoice != prefabTexture(Globals::Game::prefabs[rEntity.prefabId], rLevel.selfId)) added.back()["texture"] = writeTextureChoice(rEntity.textureChoice);
			nlohmann::ordered_json values = writeExternals(rEntity, &rLevel);
			if(!values.empty()) added.back()["externals"] = values;
		}
	}

	/* What each of the level's own entities has more or less than the game's data plus the prefab edits give it (the same order of application as applyEdits), by name; an added entity keeps its whole list. The bound area is kept whole when it differs. */
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	nlohmann::ordered_json prefabs = nlohmann::ordered_json::object();
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
		bool withModel = rPrefab.kind != PrefabKind::External;
		const PrefabEdits& rLevelEdits = prefabLevelEdits(rPrefab, rLevel.selfId);
		if(prefabEditsEmpty(rLevelEdits, withModel)) continue;
		nlohmann::ordered_json lists = writePrefabEdits(rLevelEdits, withModel);
		prefabs[Globals::Game::prefabs.names[i]] = lists;
	}
	nlohmann::ordered_json behaviors = nlohmann::ordered_json::array();
	nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
	nlohmann::ordered_json textures = nlohmann::ordered_json::array();
	nlohmann::ordered_json meshes = nlohmann::ordered_json::array();
	for(uint32_t i = 0; i < rLevel.sourceEntityCount && i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		std::vector<PoolId> expected = rLevel.sourceBehaviors[i];
		if(rEntity.prefabId != UninitializedPoolId)
		{
			applyPrefabBehaviorsTo(Globals::Game::prefabs[rEntity.prefabId], expected, rLevel.selfId);
		}
		std::vector<PoolId> current = rEntity.behaviorSetId == UninitializedPoolId ? std::vector<PoolId>() : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds;
		nlohmann::ordered_json addedNames = nlohmann::ordered_json::array();
		nlohmann::ordered_json removedNames = nlohmann::ordered_json::array();
		std::vector<PoolId> left = expected;
		for(PoolId behaviorId : current)
		{
			auto found = std::find(left.begin(), left.end(), behaviorId);
			if(found == left.end()) addedNames.push_back(rTypes.names[behaviorId.id]);
			else left.erase(found);
		}
		for(PoolId behaviorId : left) removedNames.push_back(rTypes.names[behaviorId.id]);
		if(!addedNames.empty() || !removedNames.empty()) behaviors.push_back({{"index", i}, {"added", addedNames}, {"removed", removedNames}});

		std::vector<Physics::CollisionShape> expectedShapes = rLevel.sourceShapes[i];
		if(rEntity.prefabId != UninitializedPoolId) applyPrefabShapesTo(Globals::Game::prefabs[rEntity.prefabId], expectedShapes, rLevel.selfId);
		std::vector<Physics::CollisionShape> currentShapes = entityShapes(rEntity.selfId);
		if(currentShapes != expectedShapes)
		{
			nlohmann::ordered_json shapeList = nlohmann::ordered_json::array();
			for(const Physics::CollisionShape& rShape : currentShapes) shapeList.push_back(writeShape(rShape, &rLevel));
			shapes.push_back({{"index", i}, {"shapes", shapeList}});
		}

		MeshChoice expectedMesh = rEntity.prefabId != UninitializedPoolId ? prefabMesh(Globals::Game::prefabs[rEntity.prefabId], rLevel.selfId) : MeshChoice{};
		if(rEntity.meshChoice != expectedMesh) meshes.push_back({{"index", i}, {"mesh", writeMeshChoice(rEntity.meshChoice)}});
		TextureChoice expectedTexture = rEntity.prefabId != UninitializedPoolId ? prefabTexture(Globals::Game::prefabs[rEntity.prefabId], rLevel.selfId) : TextureChoice{};
		if(rEntity.textureChoice != expectedTexture) textures.push_back({{"index", i}, {"texture", writeTextureChoice(rEntity.textureChoice)}});
	}
	nlohmann::ordered_json edits = nlohmann::ordered_json::object();
	if(!moved.empty() || !added.empty() || !prefabs.empty() || !behaviors.empty() || !shapes.empty() || !textures.empty() || !meshes.empty() || !transforms.empty() || !externals.empty())
	{
		edits = {{"moved", moved}, {"added", added}};
		if(!prefabs.empty()) edits["prefabs"] = prefabs;
		if(!behaviors.empty()) edits["behaviors"] = behaviors;
		if(!shapes.empty()) edits["shapes"] = shapes;
		if(!transforms.empty()) edits["transforms"] = transforms;
		if(!externals.empty()) edits["externals"] = externals;
		if(!meshes.empty()) edits["meshes"] = meshes;
		if(!textures.empty()) edits["textures"] = textures;
	}
	return edits;
}

/* The "cameras" list of the level as it is now: its camera entities after the source ones (the editor's Add camera and paste put them in Level::entityIds). The inverse of addLevelCameras; empty when there are none. */
static nlohmann::ordered_json buildLevelCameras(const Level& rLevel)
{
	nlohmann::ordered_json cameras = nlohmann::ordered_json::array();
	auto vector = [](const glm::vec3& rValue_) { return nlohmann::ordered_json::array({rValue_.x, rValue_.y, rValue_.z}); };
	glm::mat4 toLocal = glm::inverse(rLevel.hubWorldMatrix());
	for(uint32_t i = rLevel.sourceEntityCount; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(rEntity.cameraId == UninitializedPoolId) continue;
		const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
		nlohmann::ordered_json entry = {{"name", Globals::Game::entities.names[rLevel.entityIds[i].id]}, {"position", vector(glm::vec3(toLocal * glm::vec4(rCamera.position, 1.0f)))}, {"front", vector(rCamera.front)}, {"yaw", rCamera.yaw}, {"pitch", rCamera.pitch},
			{"fov", rCamera.fov}, {"near", rCamera.nearPlane}, {"far", rCamera.farPlane}, {"enabled", rCamera.enabled}};
		if(Globals::Game::mainCamera == rEntity.cameraId) entry["main"] = true;
		if(const Graphics::RenderTarget* pTarget = rCamera.renderTarget()) entry["renderTarget"] = {{"width", pTarget->width}, {"height", pTarget->height}};
		cameras.push_back(entry);
	}
	return cameras;
}

/* The "entities" list of the level as it is now: its plain entities after the source ones (no prefab, no camera: the editor's Add empty entity). The inverse of addLevelEntities; empty when there are none. */
static nlohmann::ordered_json buildLevelEntities(const Level& rLevel)
{
	nlohmann::ordered_json entities = nlohmann::ordered_json::array();
	auto vector = [](const glm::vec3& rValue_) { return nlohmann::ordered_json::array({rValue_.x, rValue_.y, rValue_.z}); };
	glm::mat4 toLocal = glm::inverse(rLevel.hubWorldMatrix());
	for(uint32_t i = rLevel.sourceEntityCount; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(rEntity.prefabId != UninitializedPoolId || rEntity.cameraId != UninitializedPoolId) continue;
		nlohmann::ordered_json names = nlohmann::ordered_json::array();
		if(rEntity.behaviorSetId != UninitializedPoolId)
		{
			for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) names.push_back(Globals::Game::entityBehaviorTypes.names[behaviorId.id]);
		}
		nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
		for(const Physics::CollisionShape& rShape : entityShapes(rEntity.selfId)) shapes.push_back(writeShape(rShape, &rLevel));
		nlohmann::ordered_json entry = {{"name", Globals::Game::entities.names[rLevel.entityIds[i].id]}, {"position", vector(glm::vec3(toLocal * glm::vec4(rEntity.position, 1.0f)))}, {"behaviors", names}, {"shapes", shapes}};
		if(rEntity.rotation != glm::mat4(1.0f))
		{
			glm::mat3 matrix(glm::mat3(toLocal) * glm::mat3(rEntity.rotation));
			entry["rotation"] = nlohmann::ordered_json::array({matrix[0].x, matrix[0].y, matrix[0].z, matrix[1].x, matrix[1].y, matrix[1].z, matrix[2].x, matrix[2].y, matrix[2].z});
		}
		if(rEntity.scale != glm::vec3(1.0f)) entry["scale"] = vector(rEntity.scale);
		entities.push_back(entry);
	}
	return entities;
}

bool saveLevelEdits(PoolId levelId_, const std::string& fileName_)
{
	const Level& rLevel = getLevel(levelId_);
	std::string levelName = Globals::Game::levels.names[levelId_.id];
	std::ifstream stream(levelFilePath(Paths::Levels, levelName));
	if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath(Paths::Levels, levelName)));

	/* ordered_json keeps the file's key order, so a save only changes the edits. */
	nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
	stream.close();
	file.erase("edits");
	nlohmann::ordered_json edits = buildLevelEdits(rLevel);
	if(!edits.empty()) file["edits"] = edits;
	file.erase("cameras");
	nlohmann::ordered_json cameras = buildLevelCameras(rLevel);
	if(!cameras.empty()) file["cameras"] = cameras;
	file.erase("entities");
	nlohmann::ordered_json plainEntities = buildLevelEntities(rLevel);
	if(!plainEntities.empty()) file["entities"] = plainEntities;
	file.erase("removed");
	std::vector<uint32_t> removed = rLevel.removed;
	std::sort(removed.begin(), removed.end());
	if(!removed.empty()) file["removed"] = removed;

	/*
	Saving under another name copies the level's whole folder first (its Models, Textures, Collision and Prefabs go with it), then writes the file under the new name. The copy of the old file is removed. A glTF level's chunk files are named after the level, so
	the copy of such a level does not find them (the same as before levels were folders).
	*/
	std::string text = file.dump(2);
	bool written = false;
	for(const std::string& rRoot : {std::string(Paths::Levels), std::string(Paths::SourceLevels)})
	{
		namespace fs = std::filesystem;

		/*
		Saving under the level's own name: a root that has no folder for the level (the source tree when the level only exists in Assets) is not given one, so a save does not create folders by itself; Save as makes them. A file that already holds
		exactly this text is left alone, so saving every loaded level rewrites only the levels that changed.
		*/
		if(fileName_ == levelName)
		{
			if(!fs::is_directory(rRoot + levelName)) continue;
			std::ifstream existing(levelFilePath(rRoot, fileName_));
			if(existing && std::string(std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>()) == text) continue;
		}
		if(fileName_ != levelName && fs::is_directory(rRoot + levelName))
		{
			fs::copy(rRoot + levelName, rRoot + fileName_, fs::copy_options::recursive | fs::copy_options::skip_existing);
			fs::remove(fs::path(rRoot + fileName_) / (levelName + ".json"));
		}
		fs::create_directories(rRoot + fileName_);
		std::ofstream out(levelFilePath(rRoot, fileName_));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rRoot, fileName_)));
		out << text;
		written = true;
	}
	return written;
}

namespace
{
/*
The file's "source" with the level's "edits" (buildLevelEdits: differences from that source) applied on top, as one "source" the next load gives the same level from. A per-entity entry of the edits replaces the source's entry of the same index, except "behaviors" (their added and
removed names are composed: removing what the source added only cancels it) and "moved" (the "source" position stays the source's); "added" entries follow the source's, which is the order of the entity indices. The edits' "prefabs" are not here (saveSourceLevel merges them into Prefab::source).
*/
nlohmann::ordered_json composeSource(const nlohmann::ordered_json& rSource_, const nlohmann::ordered_json& rEdits_)
{
	using OJson = nlohmann::ordered_json;
	OJson result = rSource_.is_object() ? rSource_ : OJson::object();
	auto entryOf = [](OJson& rList_, uint32_t index_) -> OJson*
	{
		for(OJson& rEntry : rList_)
		{
			if(rEntry.at("index").get<uint32_t>() == index_) return &rEntry;
		}
		return nullptr;
	};
	for(const char* pKey : {"moved", "transforms", "shapes", "meshes", "textures", "externals", "behaviors"})
	{
		if(!rEdits_.contains(pKey)) continue;
		if(!result.contains(pKey)) result[pKey] = OJson::array();
		OJson& rList = result[pKey];
		for(const OJson& rEntry : rEdits_.at(pKey))
		{
			OJson* pOld = entryOf(rList, rEntry.at("index").get<uint32_t>());
			if(pOld == nullptr)
			{
				rList.push_back(rEntry);
				continue;
			}
			std::string key = pKey;
			if(key == "moved") (*pOld)["position"] = rEntry.at("position");
			else if(key == "behaviors")
			{
				OJson& rAdded = (*pOld)["added"];
				OJson& rRemoved = (*pOld)["removed"];
				for(const OJson& rName : rEntry.value("removed", OJson::array()))
				{
					auto found = std::find(rAdded.begin(), rAdded.end(), rName);
					if(found != rAdded.end()) rAdded.erase(found);
					else if(std::find(rRemoved.begin(), rRemoved.end(), rName) == rRemoved.end()) rRemoved.push_back(rName);
				}
				for(const OJson& rName : rEntry.value("added", OJson::array()))
				{
					auto found = std::find(rRemoved.begin(), rRemoved.end(), rName);
					if(found != rRemoved.end()) rRemoved.erase(found);
					else if(std::find(rAdded.begin(), rAdded.end(), rName) == rAdded.end()) rAdded.push_back(rName);
				}
			}
			else *pOld = rEntry;
		}
	}
	if(rEdits_.contains("added") && !rEdits_.at("added").empty())
	{
		if(!result.contains("added")) result["added"] = OJson::array();
		for(const OJson& rEntry : rEdits_.at("added")) result["added"].push_back(rEntry);
	}
	/* An empty "behaviors" entry (everything cancelled) is dropped. */
	if(result.contains("behaviors"))
	{
		OJson kept = OJson::array();
		for(const OJson& rEntry : result.at("behaviors"))
		{
			if(!rEntry.value("added", OJson::array()).empty() || !rEntry.value("removed", OJson::array()).empty()) kept.push_back(rEntry);
		}
		result["behaviors"] = kept;
	}
	return result;
}
}

void saveSourceLevel(PoolId levelId_)
{
	namespace fs = std::filesystem;
	const Level& rLevel = getLevel(levelId_);
	std::string levelName = Globals::Game::levels.names[levelId_.id];
	std::ifstream stream(levelFilePath(Paths::Levels, levelName));
	if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath(Paths::Levels, levelName)));
	nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
	stream.close();

	/* The level file: its source becomes what the level is now, so there are no edits left. */
	nlohmann::ordered_json source = composeSource(file.value("source", nlohmann::ordered_json::object()), buildLevelEdits(rLevel));
	source.erase("prefabs");
	file.erase("edits");
	file.erase("source");
	if(!source.empty()) file["source"] = source;
	file.erase("cameras");
	nlohmann::ordered_json cameras = buildLevelCameras(rLevel);
	if(!cameras.empty()) file["cameras"] = cameras;
	file.erase("entities");
	nlohmann::ordered_json plainEntities = buildLevelEntities(rLevel);
	if(!plainEntities.empty()) file["entities"] = plainEntities;
	file.erase("removed");
	std::vector<uint32_t> removed = rLevel.removed;
	std::sort(removed.begin(), removed.end());
	if(!removed.empty()) file["removed"] = removed;
	std::string text = file.dump(2);

	/* The prefabs' edits go to the level's SourceGamePrefabs folder, one file per prefab (removed when the prefab has none). */
	for(const std::string& rRoot : {std::string(Paths::Levels), std::string(Paths::SourceLevels)})
	{
		for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
		{
			if(!Globals::Game::prefabs.alive[i]) continue;
			const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
			bool withModel = rPrefab.kind != PrefabKind::External;
			PrefabEdits merged = mergePrefabEdits(prefabSourceEdits(rPrefab, levelId_), prefabLevelEdits(rPrefab, levelId_));
			fs::path path = sourcePrefabFile(rRoot, levelName, Globals::Game::prefabs.names[i]);
			if(prefabEditsEmpty(merged, withModel))
			{
				fs::remove(path);
				continue;
			}
			fs::create_directories(path.parent_path());
			std::ofstream out(path);
			if(!out) throw std::runtime_error(std::format("can't write {}.", path.string()));
			out << writePrefabEdits(merged, withModel).dump(2);
		}
		std::ofstream out(levelFilePath(rRoot, levelName));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rRoot, levelName)));
		out << text;
	}
}

/* The items of a comma separated list, trimmed of spaces; empty items are dropped. */
std::vector<std::string> splitList(const std::string& text_)
{
	std::vector<std::string> result;
	size_t start = 0;
	while(start <= text_.size())
	{
		size_t end = text_.find(',', start);
		if(end == std::string::npos) end = text_.size();
		std::string item = text_.substr(start, end - start);
		size_t first = item.find_first_not_of(' ');
		size_t last = item.find_last_not_of(' ');
		if(first != std::string::npos) result.push_back(item.substr(first, last - first + 1));
		start = end + 1;
	}
	return result;
}

void createLevelFile(const NewLevelFile& rLevel_)
{
	if(rLevel_.name.empty()) throw std::runtime_error("the level needs a name.");
	for(char c : rLevel_.name)
	{
		if(!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') throw std::runtime_error("a level name can only have letters, digits, '_' and '-'.");
	}
	const std::string folders[] = {Paths::Levels, std::string(Paths::SourceLevels)};
	for(const std::string& rFolder : folders)
	{
		if(std::ifstream(levelFilePath(rFolder, rLevel_.name))) throw std::runtime_error(std::format("{} already exists.", levelFilePath(rFolder, rLevel_.name)));
	}

	nlohmann::ordered_json file;
	file["engine"] = rLevel_.engine;
	file["shader"] = rLevel_.shader;
	file["position"] = nlohmann::ordered_json::array({0, 0, 0});
	if(rLevel_.engine == "gltf")
	{
		/* The glTF files are read now, once: the chunks and collision files are written to the level's folder (GLTFImport.h) and the level file only names the collision files. */
		if(rLevel_.model.empty()) throw std::runtime_error("a gltf level needs the model file.");
		importGLTFLevelModel(rLevel_.name, rLevel_.model);
		nlohmann::ordered_json collisions = nlohmann::ordered_json::array();
		for(const std::string& rPath : splitList(rLevel_.collisions)) collisions.push_back(importGLTFLevelCollision(rLevel_.name, rPath));
		file["collisions"] = collisions;
	}
	else if(!writeGameLevelFile(rLevel_, file)) throw std::runtime_error(std::format("unknown engine '{}' (\"gltf\" or one of the game's).", rLevel_.engine));

	std::string text = file.dump(2);
	for(const std::string& rFolder : folders)
	{
		std::filesystem::create_directories(rFolder + rLevel_.name);
		std::ofstream out(levelFilePath(rFolder, rLevel_.name));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rFolder, rLevel_.name)));
		out << text;
	}
}

namespace
{
/* The glTF level the id names, with its recipe; throws if it is another engine. */
Recipe& gltfRecipeOf(PoolId levelId_)
{
	auto found = recipes.find(Globals::Game::levels.names[levelId_.id]);
	if(found == recipes.end() || found->second.gltfLevel == nullptr) throw std::runtime_error("the level is not a glTF level.");
	return found->second;
}

/* Makes the recipe's collision list the level's: written to "collisions" of both copies of the level file (the rest of the file as it is) and kept in the recipe, which a reload of the level uses. */
void writeCollisionList(Recipe& rRecipe_, const std::vector<std::string>& rNames_)
{
	const std::string& rName = rRecipe_.gltfLevel->levelName;
	for(const std::string& rFolder : {std::string(Paths::Levels), std::string(Paths::SourceLevels)})
	{
		std::ifstream stream(levelFilePath(rFolder, rName));
		if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath(rFolder, rName)));
		nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
		stream.close();
		file["collisions"] = rNames_;
		std::ofstream out(levelFilePath(rFolder, rName));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rFolder, rName)));
		out << file.dump(2);
	}
	rRecipe_.gltfLevel->collisions = rNames_;
}
}

void swapGLTFLevelModel(PoolId levelId_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	importGLTFLevelModel(rRecipe.gltfLevel->levelName, gltfPath_);
	getLevel(levelId_).swapGLTFGPUInstance(rRecipe.shaderId, *rRecipe.gltfLevel);
}

std::string addGLTFLevelCollision(PoolId levelId_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	std::string name = importGLTFLevelCollision(rRecipe.gltfLevel->levelName, gltfPath_);
	std::vector<std::string> names = rRecipe.gltfLevel->collisions;
	names.push_back(name);
	writeCollisionList(rRecipe, names);
	getLevel(levelId_).appendGLTFCollision(*rRecipe.gltfLevel, name);
	return name;
}

void swapGLTFLevelCollision(PoolId levelId_, const std::string& name_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	replaceGLTFLevelCollision(rRecipe.gltfLevel->levelName, name_, gltfPath_);
	const std::vector<std::string>& rActive = rRecipe.gltfLevel->collisions;
	if(std::find(rActive.begin(), rActive.end(), name_) != rActive.end()) getLevel(levelId_).swapGLTFCollision(*rRecipe.gltfLevel);
}

std::vector<std::string> activeGLTFCollisions(PoolId levelId_)
{
	return gltfRecipeOf(levelId_).gltfLevel->collisions;
}

bool isGLTFLevel(PoolId levelId_)
{
	auto found = recipes.find(Globals::Game::levels.names[levelId_.id]);
	return found != recipes.end() && found->second.gltfLevel != nullptr;
}
#endif
}
