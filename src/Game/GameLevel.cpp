#include "Level.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "GameLoaders.h"
#include "Globals.h"
#include "GameDiscs.h"
#include "API.h"
#include "Prefab.h"
#include "ONEInstance.h"
#include "Placement.h"
#include "RingLine.h"
#include "SASet.h"
#include "SAMesh.h"
#include "BinaryRead.h"
#include <cstdio>
#include <format>
#include <algorithm>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

/* The part of Level that is the game's (the engine side is Objects/Level.cpp; the members are declared in Game/GameLevelMembers.inl and Level.h): the SADX and Shadow instances, collision and placed entities. */

void Level::setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_, const std::vector<SATextureReplacement>& rReplacedTextures_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addSALevelInstance(shaderId_, rLandTable_, rTextures_, rSkippedTextures_, rReplacedTextures_);
	this->sourceEngine = SA_NINJA;
	this->computeBounds();
}

void Level::setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addONELevelInstance(shaderId_, rLevel_, rSkippedEntries_);
	this->sourceEngine = STH2005_ONE;
	this->computeBounds();
}

/*
Entities of a Shadow stage's placement files, see Level.h. The records are in the game's space: the level is drawn with RenderWareYawDegrees about +Y
(hypothesis, see ONEInstance.h) and ONEInstance gives every model the same yaw, so the position gets it too (as DebugSetup::addShadowPlacementMarkers does).
Which entity a type is made of is in the key (generated, see tools/genEntityKeys.py); a type it does not list (invisible triggers, effects, enemies without an archive entry) gets a marker instead (Editor::collisionView).
*/
void Level::setONEEntities(PoolId shaderId_, const STH2005::LevelKey& rLevel_)
{
	if(!this->entityIds.empty())
	{
		throw std::runtime_error("Level: already has entities.");
	}
	glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

	/*
	A record's rotation in the engine's space. The angles (degrees by their values, hypothesis) are applied about Y, then X, then Z, the order the game's ring code builds
	its matrix in (external, decompile of FUN_80112cbc; which axis is which is a hypothesis), in the game's space; the level and its objects are drawn with the yaw about the
	origin, and ONEInstance adds the yaw to every model itself, so the rotation is conjugated by the yaw: yaw * rotation * inverse(yaw), then ONEInstance's own yaw completes yaw * rotation.
	*/
	auto worldRotation = [&](const glm::vec3& rDegrees_)
	{
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(rDegrees_.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(rDegrees_.x), glm::vec3(1.0f, 0.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(rDegrees_.z), glm::vec3(0.0f, 0.0f, 1.0f));
		return yaw * rotation * glm::inverse(yaw);
	};
	for(const char* pPath : rLevel_.placementFiles)
	{
		std::vector<STH2005::Placement> placements = STH2005::readPlacements(Globals::Discs::shadow.readFile(pPath));
		for(const STH2005::Placement& rPlacement : placements)
		{
			auto found = std::find_if(rLevel_.entities.begin(), rLevel_.entities.end(), [&](const STH2005::PlacedEntityKey& rKey_)
			{
				if(rKey_.type != rPlacement.type) return false;
				return rKey_.parameter < 0 || (rPlacement.params.size() >= 4 && readLE<uint32_t>(rPlacement.params, 0) == static_cast<uint32_t>(rKey_.parameter));
			});
			if(found == rLevel_.entities.end())
			{
				/* No key for the type (invisible triggers, effects, other enemies): a cross, colored by the type, so it can be told what they are. */
				const char* pLabel = nullptr;
				if(rLevel_.typeNames != nullptr)
				{
					auto name = rLevel_.typeNames->find(rPlacement.type);
					if(name != rLevel_.typeNames->end()) pLabel = name->second;
				}
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{glm::vec3(yaw * glm::vec4(rPlacement.position, 1.0f)) + this->position, rPlacement.type, pLabel, this->selfId});
#endif
				continue;
			}
			if(found->entity.models.empty()) continue;
			const STH2005::EntityKey& rKey = found->entity;
			PoolId prefabId = findPrefab(STH2005_ONE, rKey.name);
			if(prefabId == UninitializedPoolId)
			{
				std::fprintf(stderr, "Level: skipped a record of %s: there is no prefab for it.\n", rKey.name);
				continue;
			}
			const Prefab& rPrefab = Globals::Game::prefabs[prefabId];

			/*
			What a record makes (see PlacementLayout, the prefab's): Single, one entity per model of the key's first variant (just the first model if it has none); RingLine, one per ring; Variant, one per model of the variant the record
			names, the variant's collision (the one with its index, if the key has that many) going to the first of them.
			A rotation is the record's three angles in degrees; a ring takes only the one about Y: the ring code copies one angle of the record (the one it rotates the
			line about first) into each ring (external, decompile of FUN_80112cbc; that it is the yaw is a hypothesis).
			*/
			struct Spawn
			{
				glm::vec3 gamePosition;
				glm::vec3 gameRotation;
				uint32_t modelIndex;
				std::vector<STH2005::CollisionKey> collisions;
			};
			std::vector<Spawn> spawns;
			if(rPrefab.layout == PlacementLayout::RingLine)
			{
				for(const glm::vec3& rRingPosition : STH2005::readRingPositions(rPlacement)) spawns.push_back(Spawn{rRingPosition, glm::vec3(0.0f, rPlacement.rotation.y, 0.0f), 0, rKey.collisions});
			}
			else if(rPrefab.layout == PlacementLayout::Variant)
			{
				if(rPlacement.params.size() < 4) throw std::runtime_error(std::format("Level: a record of {} has no variant parameter.", rKey.name));
				uint32_t variant = readLE<uint32_t>(rPlacement.params, 0);
				if(variant >= rPrefab.variants.size()) throw std::runtime_error(std::format("Level: a record of {} names variant {} of {}.", rKey.name, variant, rPrefab.variants.size()));
				for(uint32_t modelIndex : rPrefab.variants[variant])
				{
					std::vector<STH2005::CollisionKey> collisions;
					if(spawns.empty() && variant < rKey.collisions.size()) collisions.push_back(rKey.collisions[variant]);
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, collisions});
				}
			}
			else
			{
				/* The first variant (the .BIN's first info) is the object: all of its models, the first of them getting the collision. */
				std::vector<uint32_t> modelIndices = rPrefab.variants.empty() ? std::vector<uint32_t>{0} : rPrefab.variants[0];
				for(uint32_t modelIndex : modelIndices)
				{
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, spawns.empty() ? rKey.collisions : std::vector<STH2005::CollisionKey>{}});
				}
			}

			for(const Spawn& rSpawn : spawns)
			{
				glm::vec3 position = glm::vec3(yaw * glm::vec4(rSpawn.gamePosition, 1.0f)) + this->position;
				SpawnParams params;
				params.position = position;
				params.rotation = worldRotation(rSpawn.gameRotation);
				params.name = std::format("{}#{}", rKey.name, this->entityIds.size());
				params.what = std::format("(type {}) of {}", static_cast<unsigned>(rPlacement.type), pPath);
				params.modelIndex = rSpawn.modelIndex;
				params.pONECollisions = &rSpawn.collisions;
				params.levelId = this->selfId;
				PoolId id = spawnPrefab(prefabId, shaderId_, params);
				if(id == UninitializedPoolId) continue;
				EntityRecipe recipe;
				recipe.kind = EntityRecipe::Kind::Placed;
				recipe.prefabId = prefabId;
				recipe.shaderId = shaderId_;
				recipe.params = params;
				recipe.params.pONECollisions = nullptr;
				recipe.oneCollisions = rSpawn.collisions;
				recipe.hasOneCollisions = true;
				this->addBuilt(id, recipe);
			}
		}
	}
}

/*
The orientation an SADX SET record gives an object. External, from the decompile of _Main.rel: the objects' display code turns by the angles of their task (x at +0x14, y at +0x18, z at
+0x1C) with the Ninja rotation functions (the ones that build a rotation about 'x', 'y' or 'z' and multiply it into the current matrix on the right), and the order that comes up
most is z, then x, then y (92 functions with all three, 98 more with z and x); so the matrix is Rz * Rx * Ry, which turns the object about its own y first. Objects whose code differs have
a rotation recipe in their prefab (Prefab::rotation, from External/Prefabs/SADX/Placement.json): e.g. the dash panel turns about y by the angle plus 0x8000, the launch board turns about y before x. The angles are BAMS (65536 = a turn).
*/
static glm::mat4 saObjectRotation(const glm::ivec3& rBams_, const std::vector<SARotationStep>& rSteps_)
{
	constexpr float BamsToRadians = glm::two_pi<float>() / 65536.0f;
	if(rSteps_.empty())
	{
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), rBams_.z * BamsToRadians, glm::vec3(0.0f, 0.0f, 1.0f));
		rotation = glm::rotate(rotation, rBams_.x * BamsToRadians, glm::vec3(1.0f, 0.0f, 0.0f));
		return glm::rotate(rotation, rBams_.y * BamsToRadians, glm::vec3(0.0f, 1.0f, 0.0f));
	}
	glm::mat4 rotation(1.0f);
	for(const SARotationStep& rStep : rSteps_)
	{
		float angle = rStep.field < 3 ? static_cast<float>(rBams_[rStep.field]) : 0.0f;
		angle = (rStep.negate ? -angle : angle) + static_cast<float>(rStep.addend);
		glm::vec3 axis(rStep.axis == 'X' ? 1.0f : 0.0f, rStep.axis == 'Y' ? 1.0f : 0.0f, rStep.axis == 'Z' ? 1.0f : 0.0f);
		rotation = glm::rotate(rotation, angle * BamsToRadians, axis);
	}
	return rotation;
}

/*
Entities of an SADX stage's SET files, see Level.h. Positions are the records' own (the land table and the objects share a space, observed on Emerald Coast: the SET positions are in
the land table's range) and the rotation is the record's three BAMS angles in the order the game's object code uses most (saObjectRotation). An object that
fails to load is reported on stderr and skipped, as in setONEEntities.
*/
void Level::setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_)
{
	if(!this->entityIds.empty())
	{
		throw std::runtime_error("Level: already has entities.");
	}

	/* One entity of the level (spawnPrefab): what_ names the source in a message. */
	auto createEntity = [&](const SAEntityKey& rKey_, const SAModelKey& rModel_, const std::vector<SACollisionShapesKey>& rCollisions_, const glm::vec3& rPosition_, const glm::mat4& rRotation_, const std::string& what_, const Ninja::SAPlacement* pPlacement_)
	{
		SpawnParams params;
		params.position = rPosition_;
		params.rotation = rRotation_;
		params.name = std::format("{}#{}", rKey_.name, this->entityIds.size());
		params.what = what_;
		params.pModel = &rModel_;
		params.pCollisions = &rCollisions_;
		params.pPlacement = pPlacement_;
		params.levelId = this->selfId;
		PoolId prefabId = findPrefab(SA_NINJA, rKey_.name);
		PoolId id = spawnPrefab(prefabId, shaderId_, params);
		if(id == UninitializedPoolId) return;
		EntityRecipe recipe;
		recipe.kind = EntityRecipe::Kind::Placed;
		recipe.prefabId = prefabId;
		recipe.shaderId = shaderId_;
		recipe.params = params;
		recipe.params.pPlacement = nullptr;
		if(pPlacement_ != nullptr) recipe.placement = std::make_shared<const Ninja::SAPlacement>(*pPlacement_);
		this->addBuilt(id, recipe);
	};

	for(const char* pPath : rLandTable_.placementFiles)
	{
		std::vector<Ninja::SAPlacement> placements = Ninja::readSAPlacements(Globals::Discs::sadx.readFile(pPath));
		for(const Ninja::SAPlacement& rPlacement : placements)
		{
			glm::vec3 position = rPlacement.position + this->position;
			auto found = std::find_if(rLandTable_.entities.begin(), rLandTable_.entities.end(), [&](const SAPlacedEntityKey& rKey_) { return rKey_.type == rPlacement.type; });
			if(found == rLandTable_.entities.end())
			{
				const char* pLabel = nullptr;
				if(rLandTable_.typeNames != nullptr)
				{
					auto name = rLandTable_.typeNames->find(rPlacement.type);
					if(name != rLandTable_.typeNames->end()) pLabel = name->second;
				}
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, pLabel, this->selfId});
#endif
				continue;
			}
			const SAEntityKey& rKey = found->entity;
			const Prefab& rPrefab = Globals::Game::prefabs[findPrefab(SA_NINJA, rKey.name)];
			uint32_t variantValue = static_cast<uint32_t>(static_cast<int32_t>(rPlacement.scale[rPrefab.variantField]) & 0xFF);
			const SAModelKey& rModel = rKey.variantModels.empty() ? rKey.model : rKey.variantModels[variantValue % rKey.variantModels.size()];
			const std::vector<SACollisionShapesKey>& rCollisions = rKey.variantCollisions.empty() ? rKey.collisions : rKey.variantCollisions[variantValue % rKey.variantCollisions.size()];
			if(rModel.rel == nullptr)
			{
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, rKey.name, this->selfId});
#endif
				continue;
			}
			createEntity(rKey, rModel, rCollisions, position, saObjectRotation(rPlacement.rotation, rPrefab.rotation), std::format("(type {}) of {}", static_cast<unsigned>(rPlacement.type), pPath), &rPlacement);
		}
	}
}

void Level::setSACollision(const SALandTableKey& rLandTable_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addSACollision(rLandTable_);
	this->placeCollision();
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::setONECollision(const STH2005::LevelKey& rLevel_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addONECollision(rLevel_);
	this->placeCollision();
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::instancePieces(std::vector<std::pair<glm::vec3, float>>& rPieces_) const
{
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		for(const Graphics::SALevelPiece& rPiece : Graphics::getSALevelInstance(this->instanceId).pieces)
		{
			rPieces_.push_back({rPiece.center, rPiece.radius});
		}
		break;
	case STH2005_ONE:
	{
		glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
		for(const Graphics::ONELevelPiece& rPiece : Graphics::getONELevelInstance(this->instanceId).pieces)
		{
			rPieces_.push_back({glm::vec3(yaw * glm::vec4(rPiece.center, 1.0f)), rPiece.radius});
		}
		break;
	}
	case GLTF_FILE:
		break;
	}
}

void Level::drawInstance(const glm::mat4& rBase_, const Camera& rCamera_)
{
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSALevelInstance(this->instanceId).draw(rBase_, rCamera_);
		break;
	case STH2005_ONE:
		Graphics::getONELevelInstance(this->instanceId).draw(rBase_, rCamera_);
		break;
	case GLTF_FILE:
		break;
	}
}

unsigned int Level::instanceMeshTexture(const Graphics::BasicMesh* pMesh_) const
{
	if(this->instanceId == UninitializedPoolId) return 0;
	if(this->sourceEngine == SA_NINJA)
	{
		for(const Graphics::SALevelPiece& rPiece : Graphics::getSALevelInstance(this->instanceId).pieces)
		{
			const Graphics::SAInstance& rInstance = Graphics::getSAInstance(rPiece.saInstanceId);
			if(rInstance.textureSetId == UninitializedPoolId) continue;
			const Graphics::TextureSet& rSet = Graphics::textureSets[rInstance.textureSetId];
			for(const Graphics::SANode& rNode : Graphics::saMeshes[rInstance.saMeshId].nodes)
			{
				for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes)
				{
					if(&rSubmesh.mesh != pMesh_) continue;
					if(rSubmesh.textureIndex == Graphics::SANoTexture || rSubmesh.textureIndex >= rSet.ordered.size()) return 0;
					return Graphics::textures[rSet.ordered[rSubmesh.textureIndex]].textureId;
				}
			}
		}
	}
	else if(this->sourceEngine == STH2005_ONE)
	{
		for(const Graphics::ONELevelPiece& rPiece : Graphics::getONELevelInstance(this->instanceId).pieces)
		{
			for(const Graphics::ONELevelSubmesh& rSubmesh : rPiece.submeshes)
			{
				if(&rSubmesh.mesh == pMesh_) return rSubmesh.textureId == UninitializedPoolId ? 0 : Graphics::textures[rSubmesh.textureId].textureId;
			}
		}
	}
	return 0;
}

void Level::instanceMeshes(std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>>& rMeshes_) const
{
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		for(const Graphics::SALevelPiece& rPiece : Graphics::getSALevelInstance(this->instanceId).pieces)
		{
			const Graphics::SAMesh& rMesh = Graphics::saMeshes[Graphics::getSAInstance(rPiece.saInstanceId).saMeshId];
			std::vector<glm::mat4> world(rMesh.nodes.size());
			for(size_t i = 0; i < rMesh.nodes.size(); i++)
			{
				const Graphics::SANode& rNode = rMesh.nodes[i];
				world[i] = (rNode.parent < 0 ? glm::mat4(1.0f) : world[rNode.parent]) * rNode.localTransform;
				for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes) rMeshes_.push_back({&rSubmesh.mesh, world[i]});
			}
		}
		break;
	case STH2005_ONE:
	{
		glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
		for(const Graphics::ONELevelPiece& rPiece : Graphics::getONELevelInstance(this->instanceId).pieces)
		{
			for(const Graphics::ONELevelSubmesh& rSubmesh : rPiece.submeshes) rMeshes_.push_back({&rSubmesh.mesh, yaw});
		}
		break;
	}
	case GLTF_FILE:
		break;
	}
}

std::string Level::meshTexture(const Graphics::BasicMesh* pMesh_) const
{
	if(this->sourceEngine == SA_NINJA)
	{
		for(const Graphics::SALevelPiece& rPiece : Graphics::getSALevelInstance(this->instanceId).pieces)
		{
			const Graphics::SAInstance& rInstance = Graphics::getSAInstance(rPiece.saInstanceId);
			for(const Graphics::SANode& rNode : Graphics::saMeshes[rInstance.saMeshId].nodes)
			{
				for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes)
				{
					if(&rSubmesh.mesh != pMesh_) continue;
					if(rSubmesh.textureIndex == Graphics::SANoTexture || rInstance.textureSetId == UninitializedPoolId) return std::string();
					const Graphics::TextureSet& rSet = Graphics::textureSets[rInstance.textureSetId];
					std::string entry = rSubmesh.textureIndex < rSet.names.size() ? rSet.names[rSubmesh.textureIndex] : std::string("?");
					return std::format("{} / {} (id {})", Graphics::textureSets.getName(rInstance.textureSetId), entry, rSubmesh.textureIndex);
				}
			}
		}
	}
	else if(this->sourceEngine == STH2005_ONE)
	{
		for(const Graphics::ONELevelPiece& rPiece : Graphics::getONELevelInstance(this->instanceId).pieces)
		{
			for(const Graphics::ONELevelSubmesh& rSubmesh : rPiece.submeshes)
			{
				if(&rSubmesh.mesh == pMesh_ && rSubmesh.textureId != UninitializedPoolId) return std::format("texture pool id {}", rSubmesh.textureId.id);
			}
		}
	}
	return std::string();
}

void Level::destroyInstance()
{
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::saLevelInstances.remove(this->instanceId);
		break;
	case STH2005_ONE:
		Graphics::oneLevelInstances.remove(this->instanceId);
		break;
	case GLTF_FILE:
		break;
	}
}

void captureGameBaseline(EntityBaseline& rBaseline_, const Entity& rEntity_)
{
	rBaseline_.animationIntent = rEntity_.animationIntent;
	rBaseline_.blinkIntent = rEntity_.blinkIntent;
	rBaseline_.animationRate = rEntity_.animationRate;
	rBaseline_.animationBlendRate = rEntity_.animationBlendRate;
}

bool gameBaselineChanged(const Entity& rEntity_, const EntityBaseline& rBaseline_)
{
	if(rEntity_.animationIntent != rBaseline_.animationIntent || rEntity_.blinkIntent != rBaseline_.blinkIntent) return true;
	return rEntity_.animationRate != rBaseline_.animationRate || rEntity_.animationBlendRate != rBaseline_.animationBlendRate;
}
