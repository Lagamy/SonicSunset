#include "Level.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include "Globals.h"
#include "API.h"
#include "Prefab.h"
#ifdef EDITOR
#include "ExternalPrefabs.h"
#endif
#include <cstdio>
#include <format>
#include <algorithm>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

void Level::setGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addGLTFLevelInstance(shaderId_, rLevel_);
	this->sourceEngine = GLTF_FILE;
	this->computeBounds();
}

void Level::setPosition(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	if(this->collisionId != UninitializedPoolId)
	{
		this->placeCollision();
		Globals::Physics::collisionSystem.rebuildQuadTree();
	}
}

void Level::computeBounds()
{
	this->hasBounds = false;
	if(this->instanceId == UninitializedPoolId) return;
	glm::vec3 low(0.0f), high(0.0f);
	auto add = [&](const glm::vec3& rCenter_, float radius_)
	{
		glm::vec3 pieceLow = rCenter_ - glm::vec3(radius_);
		glm::vec3 pieceHigh = rCenter_ + glm::vec3(radius_);
		low = this->hasBounds ? glm::min(low, pieceLow) : pieceLow;
		high = this->hasBounds ? glm::max(high, pieceHigh) : pieceHigh;
		this->hasBounds = true;
	};
	if(this->sourceEngine == GLTF_FILE)
	{
		for(const Graphics::GLTFLevelSubmesh& rSubmesh : Graphics::gltfLevelInstances[this->instanceId].submeshes)
		{
			add(rSubmesh.center, rSubmesh.radius);
		}
	}
	else
	{
		std::vector<std::pair<glm::vec3, float>> pieces;
		this->instancePieces(pieces);
		for(const auto& [rCenter, radius] : pieces) add(rCenter, radius);
	}
	this->boundsMin = low;
	this->boundsMax = high;
}

glm::mat4 Level::hubWorldMatrix() const
{
	glm::mat4 matrix = glm::translate(glm::mat4(1.0f), this->hubWorldPosition);
	matrix = glm::rotate(matrix, glm::radians(this->hubWorldRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	matrix = glm::rotate(matrix, glm::radians(this->hubWorldRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	matrix = glm::rotate(matrix, glm::radians(this->hubWorldRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
	return matrix;
}

void Level::placeCollision()
{
	if(this->collisionId == UninitializedPoolId) return;
	glm::mat4 matrix = this->hubWorldMatrix();
	Physics::Collision& rCollision = Globals::Physics::collisions[this->collisionId];
	rCollision.setRotation(glm::mat3(matrix));
	rCollision.setPosition(glm::vec3(matrix * glm::vec4(this->position, 1.0f)));
}

void Level::commitHubWorldCollision()
{
	if(!this->collisionStale) return;
	this->collisionStale = false;
	this->placeCollision();
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::setHubWorldTransform(const glm::vec3& rPosition_, const glm::vec3& rRotationDegrees_, bool collision_)
{
	glm::mat4 old = this->hubWorldMatrix();
	this->hubWorldPosition = rPosition_;
	this->hubWorldRotation = rRotationDegrees_;
	glm::mat4 delta = this->hubWorldMatrix() * glm::inverse(old);
	glm::mat4 deltaRotation = glm::mat4(glm::mat3(delta));
	auto movePoint = [&](const glm::vec3& rPoint_) { return glm::vec3(delta * glm::vec4(rPoint_, 1.0f)); };

	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		Entity& rEntity = getEntity(id);
		rEntity.setPosition(movePoint(rEntity.position));
		rEntity.setRotation(deltaRotation * rEntity.rotation);
	}
	for(glm::vec3& rPosition : this->sourcePositions) rPosition = movePoint(rPosition);
	for(glm::mat4& rRotation : this->sourceRotations) rRotation = deltaRotation * rRotation;
	for(auto& [key, rBuilt] : this->built)
	{
		rBuilt.baseline.position = movePoint(rBuilt.baseline.position);
		rBuilt.baseline.rotation = deltaRotation * rBuilt.baseline.rotation;
		if(rBuilt.baseline.hasCamera) rBuilt.baseline.cameraPosition = movePoint(rBuilt.baseline.cameraPosition);
	}
	this->collisionStale = true;
	if(collision_) this->commitHubWorldCollision();
#ifdef EDITOR
	for(Graphics::CollisionView::Marker& rMarker : editor.collisionView.markers)
	{
		if(rMarker.levelId == this->selfId) rMarker.position = movePoint(rMarker.position);
	}
#endif
	Globals::Game::spatialIndex.dirty = true;
}

void Level::setGLTFCollision(const GLTFLevelKey& rLevel_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addGLTFCollision(rLevel_);
	this->placeCollision();
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::swapGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->instanceId != UninitializedPoolId)
	{
		Graphics::gltfLevelInstances.remove(this->instanceId);
		this->instanceId = UninitializedPoolId;
	}
	this->setGLTFGPUInstance(shaderId_, rLevel_);
}

void Level::swapGLTFCollision(const GLTFLevelKey& rLevel_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
	}
	if(rLevel_.collisions.empty()) Globals::Physics::collisionSystem.rebuildQuadTree();
	else this->setGLTFCollision(rLevel_);
}

void Level::appendGLTFCollision(const GLTFLevelKey& rLevel_, const std::string& rName_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->collisionId == UninitializedPoolId)
	{
		this->setGLTFCollision(rLevel_);
		return;
	}
	Globals::Physics::collisions[this->collisionId].setRotation(glm::mat3(1.0f));
	Loading::appendGLTFCollision(this->collisionId, rLevel_, rName_);
	this->placeCollision();
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;
}

void Level::draw(const Camera& rCamera_)
{
	if(this->instanceId == UninitializedPoolId) return;
	glm::mat4 base = this->hubWorldMatrix() * glm::translate(glm::mat4(1.0f), this->position);
	if(this->sourceEngine == GLTF_FILE) Graphics::gltfLevelInstances[this->instanceId].draw(base, rCamera_);
	else this->drawInstance(base, rCamera_);
}

bool Level::rayHitsInstance(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, const Graphics::BasicMesh** ppMesh_)
{
	if(this->instanceId == UninitializedPoolId) return false;
	glm::mat4 base = this->hubWorldMatrix() * glm::translate(glm::mat4(1.0f), this->position);
	std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>> meshes;
	if(this->sourceEngine == GLTF_FILE)
	{
		for(const Graphics::GLTFLevelSubmesh& rSubmesh : Graphics::gltfLevelInstances[this->instanceId].submeshes) meshes.push_back({&rSubmesh.mesh, glm::mat4(1.0f)});
	}
	else this->instanceMeshes(meshes);

	/* The ray goes into the level's space once; the meshes are placed in it. */
	glm::mat4 inverse = glm::inverse(base);
	glm::vec3 origin = glm::vec3(inverse * glm::vec4(rOrigin_, 1.0f));
	glm::vec3 direction = glm::vec3(inverse * glm::vec4(rDirection_, 0.0f));
	bool hit = false;
	for(const auto& [pMesh, rWorld] : meshes)
	{
		float distance = 0.0f;
		bool any = false;
		if(rayHitsMesh(*pMesh, rWorld, origin, direction, distance, any) && (!hit || distance < rDistance_))
		{
			rDistance_ = distance;
			if(ppMesh_ != nullptr) *ppMesh_ = pMesh;
			hit = true;
		}
	}
	return hit;
}

void Level::addBuilt(PoolId entityId_, const EntityRecipe& rRecipe_)
{
	uint32_t index = static_cast<uint32_t>(this->entityIds.size());
	this->entityIds.push_back(entityId_);
	if(entityId_ == UninitializedPoolId) return;
	BuiltEntity& rBuilt = this->built[builtKey(entityId_)];
	rBuilt.index = index;
	rBuilt.recipe = rRecipe_;
}

void Level::captureBaseline(PoolId entityId_)
{
	auto found = this->built.find(builtKey(entityId_));
	if(found == this->built.end() || !entityExists(entityId_)) return;
	const Entity& rEntity = getEntity(entityId_);
	EntityBaseline& rBaseline = found->second.baseline;
	rBaseline.position = rEntity.position;
	rBaseline.rotation = rEntity.rotation;
	rBaseline.scale = rEntity.scale;
	rBaseline.modelMatrix = rEntity.modelMatrix;
	rBaseline.behaviors = rEntity.behaviorSetId == UninitializedPoolId ? std::vector<PoolId>() : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds;
	rBaseline.shapes = entityShapes(entityId_);
	rBaseline.mesh = entityMesh(entityId_);
	rBaseline.texture = entityTexture(entityId_);
	captureGameBaseline(rBaseline, rEntity);
	rBaseline.behaviorNotCulled = rEntity.behaviorNotCulled;
	rBaseline.hasCamera = rEntity.cameraId != UninitializedPoolId;
	if(rBaseline.hasCamera)
	{
		const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
		rBaseline.cameraPosition = rCamera.position;
		rBaseline.cameraFront = rCamera.front;
		rBaseline.cameraYaw = rCamera.yaw;
		rBaseline.cameraPitch = rCamera.pitch;
		rBaseline.cameraFov = rCamera.fov;
		rBaseline.cameraNear = rCamera.nearPlane;
		rBaseline.cameraFar = rCamera.farPlane;
		rBaseline.cameraEnabled = rCamera.enabled;
		rBaseline.cameraIsMain = Globals::Game::mainCamera == rEntity.cameraId;
		rBaseline.cameraRenderTargetId = rCamera.renderTargetId;
	}
}

void Level::captureBaselines()
{
	this->builtCount = static_cast<uint32_t>(this->entityIds.size());
	for(PoolId id : this->entityIds)
	{
		if(id != UninitializedPoolId) this->captureBaseline(id);
	}
}

/*
What can differ in an entity after it ran: its transform, behaviour list, bound area, model, texture, motion, uncullable flag and the Lua state (the dirty flag). A physics body or an entity state always counts as changed: its contents (velocity) are not compared. A camera module is compared by its view (position, direction, field of view, planes, enabled, render target, whether it is the main camera).
Effects other systems keep by entity (the dash ribbon, the effect balls: player effects) are not looked at.
*/
bool Level::entityChanged(PoolId entityId_) const
{
	auto found = this->built.find(builtKey(entityId_));
	if(found == this->built.end() || !entityExists(entityId_)) return true;
	const Entity& rEntity = getEntity(entityId_);
	const EntityBaseline& rBaseline = found->second.baseline;
	if(rEntity.scriptStateDirty || rEntity.physicsId != UninitializedPoolId || rEntity.stateId != UninitializedPoolId) return true;
	if((rEntity.cameraId != UninitializedPoolId) != rBaseline.hasCamera) return true;
	if(rBaseline.hasCamera)
	{
		const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
		if(rCamera.position != rBaseline.cameraPosition || rCamera.front != rBaseline.cameraFront || rCamera.yaw != rBaseline.cameraYaw || rCamera.pitch != rBaseline.cameraPitch) return true;
		if(rCamera.fov != rBaseline.cameraFov || rCamera.nearPlane != rBaseline.cameraNear || rCamera.farPlane != rBaseline.cameraFar || rCamera.enabled != rBaseline.cameraEnabled) return true;
		if(rCamera.renderTargetId != rBaseline.cameraRenderTargetId || (Globals::Game::mainCamera == rEntity.cameraId) != rBaseline.cameraIsMain) return true;
	}
	if(rEntity.position != rBaseline.position || rEntity.rotation != rBaseline.rotation || rEntity.scale != rBaseline.scale || rEntity.modelMatrix != rBaseline.modelMatrix) return true;
	if(gameBaselineChanged(rEntity, rBaseline) || rEntity.behaviorNotCulled != rBaseline.behaviorNotCulled) return true;
	static const std::vector<PoolId> none;
	const std::vector<PoolId>& rBehaviors = rEntity.behaviorSetId == UninitializedPoolId ? none : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds;
	if(rBehaviors != rBaseline.behaviors) return true;
	if(entityMesh(entityId_) != rBaseline.mesh || entityTexture(entityId_) != rBaseline.texture) return true;
	return entityShapes(entityId_) != rBaseline.shapes;
}

/* Removes the level's instance, which gives back the users of its pieces. Called by PoolMap::remove. */
void Level::markSourceEntities()
{
	this->sourceEntityCount = static_cast<uint32_t>(this->entityIds.size());
	this->sourcePositions.clear();
	this->sourceRotations.clear();
	this->sourceScales.clear();
	this->sourceBehaviors.clear();
	this->sourceShapes.clear();
	for(PoolId id : this->entityIds)
	{
		/* A gone entity (an entry of the saved source that could not be made) keeps its index with empty values. */
		if(!entityExists(id))
		{
			this->sourcePositions.push_back(glm::vec3(0.0f));
			this->sourceRotations.push_back(glm::mat4(1.0f));
			this->sourceScales.push_back(glm::vec3(1.0f));
			this->sourceBehaviors.emplace_back();
			this->sourceShapes.emplace_back();
			continue;
		}
		const Entity& rEntity = getEntity(id);
		this->sourcePositions.push_back(rEntity.position);
		this->sourceRotations.push_back(rEntity.rotation);
		this->sourceScales.push_back(rEntity.scale);
		this->sourceBehaviors.push_back(rEntity.behaviorSetId == UninitializedPoolId ? std::vector<PoolId>() : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds);
		this->sourceShapes.push_back(entityShapes(id));
	}
}

int32_t Level::sourceIndexOf(PoolId entityId_) const
{
	for(uint32_t i = 0; i < this->sourceEntityCount; i++)
	{
		if(this->entityIds[i].id == entityId_.id && this->entityIds[i].generation == entityId_.generation) return static_cast<int32_t>(i);
	}
	return -1;
}

int32_t Level::editIndexOf(PoolId entityId_) const
{
	if(entityId_ == UninitializedPoolId) return -1;
	uint32_t added = this->sourceEntityCount;
	for(uint32_t i = 0; i < this->entityIds.size(); i++)
	{
		bool own = i < this->sourceEntityCount;
		if(this->entityIds[i] == entityId_)
		{
			if(own) return static_cast<int32_t>(i);
			return entityExists(entityId_) && getEntity(entityId_).prefabId != UninitializedPoolId ? static_cast<int32_t>(added) : -1;
		}
		if(!own && entityExists(this->entityIds[i]) && getEntity(this->entityIds[i]).prefabId != UninitializedPoolId) added++;
	}
	return -1;
}

void Level::resolveExternalEntities()
{
	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		for(auto& [behaviorName, rValues] : getEntity(id).externals)
		{
			for(auto& [fieldName, rValue] : rValues)
			{
				if(rValue.entityIndex < 0) continue;
				bool found = static_cast<size_t>(rValue.entityIndex) < this->entityIds.size() && entityExists(this->entityIds[rValue.entityIndex]);
				rValue.entity = found ? this->entityIds[rValue.entityIndex] : UninitializedPoolId;
				rValue.entityIndex = -1;
			}
		}
		/* The values of the entity's collision shapes (Physics::CollisionShape::values) refer to entities the same way. */
		PoolId collisionId = getEntity(id).collisionId;
		if(collisionId == UninitializedPoolId) continue;
		for(Physics::CollisionShape& rShape : Globals::Physics::collisions[collisionId].shapes)
		{
			for(auto& [fieldName, rValue] : rShape.values)
			{
				if(rValue.entityIndex < 0) continue;
				bool found = static_cast<size_t>(rValue.entityIndex) < this->entityIds.size() && entityExists(this->entityIds[rValue.entityIndex]);
				rValue.entity = found ? this->entityIds[rValue.entityIndex] : UninitializedPoolId;
				rValue.entityIndex = -1;
			}
		}
	}
}

std::vector<PoolId> Level::referencedPrefabs() const
{
	std::vector<PoolId> found;
	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		for(const auto& [behaviorName, rValues] : getEntity(id).externals)
		{
			for(const auto& [fieldName, rValue] : rValues)
			{
				PoolId prefab = rValue.prefab;
				if(prefab.id >= Globals::Game::prefabs.objects.size() || !Globals::Game::prefabs.alive[prefab.id] || Globals::Game::prefabs.generation[prefab.id] != prefab.generation) continue;
				if(std::find(found.begin(), found.end(), prefab) == found.end()) found.push_back(prefab);
			}
		}
		PoolId collisionId = getEntity(id).collisionId;
		if(collisionId == UninitializedPoolId) continue;
		for(const Physics::CollisionShape& rShape : Globals::Physics::collisions[collisionId].shapes)
		{
			for(const auto& [fieldName, rValue] : rShape.values)
			{
				PoolId prefab = rValue.prefab;
				if(prefab.id >= Globals::Game::prefabs.objects.size() || !Globals::Game::prefabs.alive[prefab.id] || Globals::Game::prefabs.generation[prefab.id] != prefab.generation) continue;
				if(std::find(found.begin(), found.end(), prefab) == found.end()) found.push_back(prefab);
			}
		}
	}
	return found;
}

void Level::removeSourceEntity(uint32_t index_)
{
	if(index_ >= this->sourceEntityCount || index_ >= this->entityIds.size()) return;
	if(!this->isRemoved(index_)) this->removed.push_back(index_);
	if(entityExists(this->entityIds[index_])) Globals::Game::entities.remove(this->entityIds[index_]);
	Globals::Game::spatialIndex.dirty = true;
}

bool Level::isRemoved(uint32_t index_) const
{
	return std::find(this->removed.begin(), this->removed.end(), index_) != this->removed.end();
}

void Level::revertSourceEntity(uint32_t index_)
{
	if(index_ >= this->sourceEntityCount || !entityExists(this->entityIds[index_])) return;
	Entity& rEntity = getEntity(this->entityIds[index_]);
	rEntity.setPosition(this->sourcePositions[index_]);
	rEntity.setRotation(this->sourceRotations[index_]);
	rEntity.setScale(this->sourceScales[index_]);
}

void Level::restoreSource()
{
	clearPrefabEdits(this->selfId);
	for(uint32_t i = 0; i < this->sourceEntityCount; i++)
	{
		this->revertSourceEntity(i);
		if(!entityExists(this->entityIds[i])) continue;
		getEntity(this->entityIds[i]).externals.clear();
		getEntity(this->entityIds[i]).externalsApplied.clear();
		getEntity(this->entityIds[i]).externalsKept.clear();
		setEntityBehaviors(this->entityIds[i], this->sourceBehaviors[i]);
		setEntityShapes(this->entityIds[i], this->sourceShapes[i]);
		if(entityMesh(this->entityIds[i]).kind != MeshChoiceKind::Default) setEntityMesh(this->entityIds[i], MeshChoice{});
		if(entityTexture(this->entityIds[i]).kind != TextureChoiceKind::Default) setEntityTexture(this->entityIds[i], TextureChoice{});
	}
	for(uint32_t i = this->sourceEntityCount; i < this->entityIds.size(); i++)
	{
		this->built.erase(builtKey(this->entityIds[i]));
		if(entityExists(this->entityIds[i])) Globals::Game::entities.remove(this->entityIds[i]);
	}
	this->entityIds.resize(this->sourceEntityCount);
	this->builtCount = std::min(this->builtCount, this->sourceEntityCount);
}

void Level::destroyEntities()
{
#ifdef EDITOR
	/* Only the markers this level made go; the others belong to the levels still loaded. */
	std::erase_if(editor.collisionView.markers, [&](const Graphics::CollisionView::Marker& rMarker_) { return rMarker_.levelId == this->selfId; });
#endif
	/* A level camera's render target is a pool object of its own that removing the camera does not free: the ones the build made go with it (not a target a script gave the camera, which the script owns). */
	Pool<Graphics::RenderTarget>& rTargets = Globals::Game::renderTargets;
	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		auto builtIt = this->built.find(builtKey(id));
		const Entity& rEntity = getEntity(id);
		if(builtIt != this->built.end() && builtIt->second.recipe.kind == EntityRecipe::Kind::Camera && rEntity.cameraId != UninitializedPoolId)
		{
			PoolId targetId = Globals::Game::cameras[rEntity.cameraId].renderTargetId;
			bool targetAlive = targetId != UninitializedPoolId && targetId.id < rTargets.alive.size() && rTargets.alive[targetId.id] && rTargets.generation[targetId.id] == targetId.generation;
			if(targetAlive && targetId == builtIt->second.baseline.cameraRenderTargetId) rTargets.remove(targetId);
		}
	}
	for(PoolId id : this->entityIds)
	{
		if(entityExists(id)) Globals::Game::entities.remove(id);
	}
	this->entityIds.clear();
	this->built.clear();
	this->builtCount = 0;
	this->sourceEntityCount = 0;
	this->removed.clear();
	this->sourcePositions.clear();
	this->sourceRotations.clear();
	this->sourceScales.clear();
	this->sourceBehaviors.clear();
	this->sourceShapes.clear();
	forgetLevelPrefabEdits(this->selfId);
}

void Level::destroy()
{
	this->destroyEntities();
#ifdef EDITOR
	/* The level's own External prefabs go with its entities. */
	Loading::unloadLevelPrefabs(this->name);
	/* Its External models and textures go after them: no entity of it holds a pointer into the pools any more. */
	if(!this->name.empty()) Loading::forgetExternalAssets(this->name);
#endif
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
		Globals::Physics::collisionSystem.rebuildQuadTree();
	}
	if(this->instanceId == UninitializedPoolId) return;
	if(this->sourceEngine == GLTF_FILE) Graphics::gltfLevelInstances.remove(this->instanceId);
	else this->destroyInstance();
	this->instanceId = UninitializedPoolId;
}
