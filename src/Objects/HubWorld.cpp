#include "HubWorld.h"
#include "Paths.h"
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <stdexcept>

namespace
{
using Json = nlohmann::ordered_json;

glm::vec3 readVector(const Json& rValue_)
{
	return glm::vec3(rValue_.at(0).get<float>(), rValue_.at(1).get<float>(), rValue_.at(2).get<float>());
}

Json writeVector(const glm::vec3& rValue_)
{
	return Json::array({rValue_.x, rValue_.y, rValue_.z});
}
}

void HubWorld::read()
{
	this->levels.clear();
	this->entities.clear();
	std::ifstream stream(Paths::HubWorld);
	if(!stream) return;
	try
	{
		Json file = Json::parse(stream);
		for(const Json& rEntry : file.at("levels"))
		{
			HubWorldLevel level;
			level.name = rEntry.at("name").get<std::string>();
			if(rEntry.contains("position")) level.position = readVector(rEntry.at("position"));
			if(rEntry.contains("rotation")) level.rotation = readVector(rEntry.at("rotation"));
			this->levels.push_back(level);
		}
		for(const Json& rEntry : file.value("entities", Json::array()))
		{
			HubWorldEntity entity;
			entity.name = rEntry.at("name").get<std::string>();
			entity.prefab = rEntry.value("prefab", std::string());
			if(rEntry.contains("position")) entity.position = readVector(rEntry.at("position"));
			if(rEntry.contains("scale")) entity.scale = readVector(rEntry.at("scale"));
			if(rEntry.contains("state")) entity.state = rEntry.at("state").dump();
			if(rEntry.contains("rotation"))
			{
				std::vector<float> values = rEntry.at("rotation").get<std::vector<float>>();
				if(values.size() != 9) throw std::runtime_error("an entity's rotation needs 9 numbers.");
				entity.rotation = glm::make_mat3(values.data());
			}
			this->entities.push_back(entity);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Assets/HubWorld.json not read, no HubWorld: %s\n", rError.what());
		this->levels.clear();
		this->entities.clear();
	}
}

const HubWorldLevel* HubWorld::find(const std::string& rName_) const
{
	for(const HubWorldLevel& rLevel : this->levels)
	{
		if(rLevel.name == rName_) return &rLevel;
	}
	return nullptr;
}

void HubWorld::set(const HubWorldLevel& rLevel_)
{
	for(HubWorldLevel& rLevel : this->levels)
	{
		if(rLevel.name == rLevel_.name)
		{
			rLevel = rLevel_;
			return;
		}
	}
	this->levels.push_back(rLevel_);
}

void HubWorld::remove(const std::string& rName_)
{
	std::erase_if(this->levels, [&](const HubWorldLevel& rLevel_) { return rLevel_.name == rName_; });
}

#ifdef EDITOR
void HubWorld::write() const
{
	Json levels = Json::array();
	for(const HubWorldLevel& rLevel : this->levels) levels.push_back({{"name", rLevel.name}, {"position", writeVector(rLevel.position)}, {"rotation", writeVector(rLevel.rotation)}});
	Json entities = Json::array();
	for(const HubWorldEntity& rEntity : this->entities)
	{
		const float* pRotation = glm::value_ptr(rEntity.rotation);
		entities.push_back({{"name", rEntity.name}, {"prefab", rEntity.prefab}, {"position", writeVector(rEntity.position)}, {"rotation", Json::array({pRotation[0], pRotation[1], pRotation[2], pRotation[3], pRotation[4], pRotation[5], pRotation[6], pRotation[7], pRotation[8]})}, {"scale", writeVector(rEntity.scale)}});
		if(!rEntity.state.empty()) entities.back()["state"] = Json::parse(rEntity.state);
	}
	std::string text = Json{{"levels", levels}, {"entities", entities}}.dump(2);
	for(const std::string& rPath : {std::string(Paths::HubWorld), std::string(Paths::SourceHubWorld)})
	{
		std::ofstream out(rPath);
		if(!out) throw std::runtime_error(std::format("can't write {}.", rPath));
		out << text;
	}
}
#endif
