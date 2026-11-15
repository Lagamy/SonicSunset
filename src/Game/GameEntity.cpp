#include "Entity.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "GameLoaders.h"
#include "Globals.h"
#include <cstdio>
#include <format>
#include <set>
#include <stdexcept>

/*
The part of Entity that is the game's (the engine side is Objects/Entity.cpp; the members are declared in Game/GameEntityMembers.inl and, for what depends on the engine of the instance, in Entity.h): the SADX and Shadow instances, their motions, disc textures and models, and their collision shapes.
*/

void Entity::setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_)
{
	this->checkNoInstance();
	this->instanceId = Loading::addSAInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = SA_NINJA;
	this->refreshBounds();
}

void Entity::setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_)
{
	this->checkNoInstance();
	this->instanceId = Loading::addSAInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = SA_NINJA;
	this->refreshBounds();
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_)
{
	this->checkNoInstance();
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = STH2005_ONE;
	this->refreshBounds();
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_)
{
	this->checkNoInstance();
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = STH2005_ONE;
	this->refreshBounds();
}

static Graphics::ONEInstance& getActiveONEInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId == UninitializedPoolId || rEntity_.sourceEngine != STH2005_ONE)
	{
		throw std::runtime_error("Entity: has no ONE instance.");
	}
	return Graphics::getONEInstance(rEntity_.instanceId);
}

void Entity::changeActiveMesh(const STH2005::ModelKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveMesh(rKey_);
	this->refreshBounds();
}

void Entity::switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_)
{
	getActiveONEInstance(*this).switchMeshPart(rPrevious_, rDesired_);
	this->refreshBounds();
}

void Entity::changeActiveTexture(const STH2005::TextureKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveTexture(rKey_);
}

/* Makes an SA instance draw another mesh with its textures (Loading::changeSAMesh); its motion stops, so play one after. */
void Entity::changeSAMesh(const SAModelKey& rKey_)
{
	if(this->instanceId == UninitializedPoolId || this->sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	Loading::changeSAMesh(this->instanceId, rKey_);
	this->refreshBounds();
}

static Graphics::SAInstance& getActiveSAInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId == UninitializedPoolId || rEntity_.sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	return Graphics::getSAInstance(rEntity_.instanceId);
}

void Entity::setSAIcon(uint32_t ownTexture_, uint32_t texture_)
{
	Graphics::SAInstance& rInstance = getActiveSAInstance(*this);
	const Graphics::SAMesh& rMesh = Graphics::saMeshes[rInstance.saMeshId];
	rInstance.iconNode = -1;
	for(size_t i = 0; i < rMesh.nodes.size() && rInstance.iconNode < 0; i++)
	{
		for(const Graphics::SASubmesh& rSubmesh : rMesh.nodes[i].submeshes)
		{
			if(rSubmesh.textureIndex == ownTexture_) rInstance.iconNode = static_cast<int32_t>(i);
		}
	}
	if(rInstance.iconNode < 0) throw std::runtime_error("Entity: no node has the icon's own texture.");
	if(rInstance.textureSetId == UninitializedPoolId || texture_ >= Graphics::textureSets[rInstance.textureSetId].ordered.size()) throw std::runtime_error("Entity: the icon texture is not in the texture set.");
	rInstance.iconTextureId = Graphics::textureSets[rInstance.textureSetId].ordered[texture_];
}

std::string Entity::discTexture() const
{
	if(this->sourceEngine == SA_NINJA)
	{
		const Graphics::SAInstance& rInstance = getActiveSAInstance(*this);
		return rInstance.textureSetId == UninitializedPoolId ? std::string() : Graphics::textureSets.getName(rInstance.textureSetId);
	}
	const Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	if(rInstance.textureDictionaryIndex == STH2005::NoIndex) return std::string();
	return Graphics::oneArchives[rInstance.archiveId].textureDictionaryNames[rInstance.textureDictionaryIndex];
}

void Entity::setDiscTexture(const std::string& rName_)
{
	if(this->sourceEngine == SA_NINJA)
	{
		getActiveSAInstance(*this);
		SATextureSetKey key{rName_.c_str()};
		Loading::changeSATextureSet(this->instanceId, rName_.empty() ? nullptr : &key);
		return;
	}
	Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	if(rName_.empty()) return;
	STH2005::TextureKey key{STH2005::ArchiveKey{Graphics::oneArchives.getName(rInstance.archiveId).c_str()}, rName_.c_str()};
	rInstance.changeActiveTexture(key);
}

std::vector<std::string> Entity::discTextureOptions() const
{
	std::vector<std::string> names;
	if(this->sourceEngine != STH2005_ONE) return names;
	const STH2005::ONE& rArchive = Graphics::oneArchives[getActiveONEInstance(*this).archiveId];
	for(const STH2005::ONEEntry& rEntry : rArchive.entries)
	{
		if(rEntry.name.size() > 4 && (rEntry.name.ends_with(".TXD") || rEntry.name.ends_with(".txd"))) names.push_back(rEntry.name);
	}
	return names;
}

/* A string that lives for the rest of the run: a Shadow mesh keeps the strings of the recipe it was built from (STH2005::ModelPart). */
static const char* keepString(const std::string& rText_)
{
	static std::set<std::string> kept;
	return kept.insert(rText_).first->c_str();
}

std::string Entity::discMesh() const
{
	if(this->sourceEngine == SA_NINJA) return Graphics::saMeshes.getName(getActiveSAInstance(*this).saMeshId);
	const Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	return Graphics::oneArchives[rInstance.archiveId].meshNames[rInstance.meshIndex];
}

void Entity::setDiscMesh(const std::string& rName_)
{
	if(this->sourceEngine == SA_NINJA)
	{
		getActiveSAInstance(*this);
		size_t at = rName_.rfind("@0x");
		if(at == std::string::npos) throw std::runtime_error(std::format("'{}' is not a SADX model (<rel>@0x<offset>)", rName_));
		std::string rel = rName_.substr(0, at);
		SAModelKey key{rel.c_str(), static_cast<uint32_t>(std::stoul(rName_.substr(at + 3), nullptr, 16))};
		std::string previous = this->discMesh();
		Loading::changeSAMesh(this->instanceId, key);
		if(this->discMesh() != previous)
		{
			this->animationIntent = AnimationIntent{};
			this->appliedAnimation = AnimationIntent{};
			this->blinkIntent = AnimationIntent{};
			this->appliedBase = AnimationIntent{};
		}
		this->refreshBounds();
		return;
	}
	Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	const STH2005::ONE& rArchive = Graphics::oneArchives[rInstance.archiveId];
	STH2005::ModelKey key{STH2005::ArchiveKey{keepString(Graphics::oneArchives.getName(rInstance.archiveId))}, nullptr, {}};
	auto found = rArchive.meshByName.find(rName_);
	if(found != rArchive.meshByName.end())
	{
		key.name = keepString(rName_);
		key.parts = rArchive.meshParts[found->second];
	}
	else if(rName_.starts_with("(mesh)/"))
	{
		key.name = keepString(rName_);
		key.parts = {STH2005::ModelPart{keepString(rName_.substr(7)), nullptr, nullptr}};
	}
	else throw std::runtime_error(std::format("'{}' is not a model of this archive", rName_));
	rInstance.changeActiveMesh(key);
	this->refreshBounds();
}

std::vector<std::string> Entity::discMeshOptions() const
{
	std::vector<std::string> names;
	if(this->sourceEngine != STH2005_ONE) return names;
	const STH2005::ONE& rArchive = Graphics::oneArchives[getActiveONEInstance(*this).archiveId];
	for(const STH2005::ONEEntry& rEntry : rArchive.entries)
	{
		if(rEntry.name.size() > 4 && (rEntry.name.ends_with(".DFF") || rEntry.name.ends_with(".dff"))) names.push_back(rEntry.name);
	}
	return names;
}

void Entity::playSAMotion(const SAMotionKey& rKey_)
{
	if(this->instanceId == UninitializedPoolId || this->sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	Loading::playSAMotion(this->instanceId, rKey_);
}

void Entity::playONEMotion(const STH2005::MotionKey& rKey_, bool loop_)
{
	getActiveONEInstance(*this).playMotion(rKey_, loop_);
}

void Entity::setONEPose(const std::vector<Graphics::PoseRequest>& rLayers_)
{
	getActiveONEInstance(*this).setPose(rLayers_);
}

void Entity::updateInstance(float deltaTime_)
{
	/* Gameplay only says what it wants; the instance changes here, once a frame, and only if the wish changed. A motion that can't be played is reported once (it counts as applied). */
	bool baseChanged = this->animationIntent != this->appliedBase;
	if(baseChanged)
	{
		this->appliedBase = this->animationIntent;
		this->baseFrame = 0.0f;
		this->baseFrameCount = 0;
	}
	const bool blinking = this->blinkIntent.pSAMotion != nullptr && this->sourceEngine == SA_NINJA;
	const AnimationIntent& rWanted = blinking ? this->blinkIntent : this->animationIntent;
	if(rWanted != this->appliedAnimation)
	{
		this->appliedAnimation = rWanted;
		try
		{
			if(rWanted.pSAMotion != nullptr && this->sourceEngine == SA_NINJA)
			{
				/*
				A change of the wanted motion blends from the pose shown now (SAInstance::beginBlend); the start or end of a blink doesn't. The game doesn't blend between motions of different models
				(external, decomp FUN_804cf4f4), so a mesh that changes with the motion cancels it.
				*/
				Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
				const bool blend = baseChanged && !blinking && this->animationBlendRate > 0.0f;
				const std::string previousMesh = blend ? this->discMesh() : std::string();
				if(blend) rInstance.beginBlend(this->animationBlendRate);
				else rInstance.cancelBlend();
				if(rWanted.pSAMesh != nullptr) this->changeSAMesh(*rWanted.pSAMesh);
				this->playSAMotion(*rWanted.pSAMotion);
				rInstance.loop = rWanted.loop;
				if(blend && this->discMesh() != previousMesh) rInstance.cancelBlend();
				if(!blinking)
				{
					Graphics::SAInstance& rPlaying = Graphics::getSAInstance(this->instanceId);
					rPlaying.animationFrame = this->baseFrame;
					/* A motion asked for backwards without a blend starts at its last frame (the blend case: SAInstance::update). */
					if(rPlaying.blendRate <= 0.0f && baseChanged && this->animationRate < 0.0f && rPlaying.currentSAAnimation != UninitializedPoolId) rPlaying.animationFrame = static_cast<float>(Graphics::saMotions[rPlaying.currentSAAnimation].frameCount - 1);
				}
			}
			else if(rWanted.pONEMotion != nullptr && this->sourceEngine == STH2005_ONE) this->playONEMotion(*rWanted.pONEMotion, rWanted.loop);
		}
		catch(const std::runtime_error& rError)
		{
			std::fprintf(stderr, "Entity: could not play the wanted motion: %s\n", rError.what());
		}
	}
	switch(this->sourceEngine)
	{
	case SA_NINJA:
	{
		Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
		rInstance.framesPerSecond = this->animationRate;
		rInstance.update(deltaTime_);
		if(!blinking)
		{
			this->baseFrame = rInstance.animationFrame;
			if(rInstance.currentSAAnimation != UninitializedPoolId) this->baseFrameCount = Graphics::saMotions[rInstance.currentSAAnimation].frameCount;
		}
		else if(this->baseFrameCount != 0) this->baseFrame = Graphics::advanceMotionFrame(this->baseFrame, deltaTime_ * rInstance.playbackRate(), this->baseFrameCount, this->animationIntent.loop);
		break;
	}
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).speed = this->animationRate > 0.0f ? this->animationRate : 1.0f;
		Graphics::getONEInstance(this->instanceId).update(deltaTime_);
		break;
	/* Returned above: an external-only entity has no instance to update. */
	case GLTF_FILE:
		break;
	}
}

Graphics::BoundingSphere Entity::instanceBounds()
{
	Graphics::BoundingSphere bounds;
	if(this->sourceEngine == GLTF_FILE) return bounds;
	if(this->sourceEngine == SA_NINJA)
	{
		Graphics::SAMesh& rMesh = Graphics::saMeshes[Graphics::getSAInstance(this->instanceId).saMeshId];
		rMesh.computeBounds();
		bounds = rMesh.bounds;
	}
	else
	{
		const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(this->instanceId);
		Graphics::RenderWareMesh& rMesh = Graphics::oneArchives[rInstance.archiveId].meshes[rInstance.meshIndex];
		rMesh.computeBounds();
		bounds = rMesh.bounds;

		/* A ONE instance turns the model by RenderWareYawDegrees about y before the entity's transform. */
		bounds.center = glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::vec4(bounds.center, 1.0f));
	}
	return bounds;
}

void Entity::setInstanceOverrideTexture(Graphics::Texture* pTexture_)
{
	if(this->sourceEngine == SA_NINJA) getActiveSAInstance(*this).pOverrideTexture = pTexture_;
	else getActiveONEInstance(*this).pOverrideTexture = pTexture_;
}

/* A ONE instance turns its model by RenderWareYawDegrees about y before the entity's transform. */
glm::mat4 Entity::instanceTurn() const
{
	if(this->sourceEngine != STH2005_ONE) return glm::mat4(1.0f);
	return glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
}

/* An SA model with a pose built (it is animated). */
bool Entity::instancePosed() const
{
	return this->sourceEngine == SA_NINJA && !Graphics::getSAInstance(this->instanceId).nodePose.empty();
}

void Entity::instanceMeshes(std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>>& rMeshes_, bool rest_) const
{
	if(this->sourceEngine == SA_NINJA)
	{
		const Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
		const Graphics::SAMesh& rMesh = Graphics::saMeshes[rInstance.saMeshId];

		/* The pose of the last drawn frame places the nodes when there is one (it overrides each node's localTransform). */
		bool usePose = !rest_ && rInstance.nodePose.size() == rMesh.nodes.size();
		std::vector<glm::mat4> world(rMesh.nodes.size());
		for(size_t i = 0; i < rMesh.nodes.size(); i++)
		{
			const Graphics::SANode& rNode = rMesh.nodes[i];
			world[i] = (rNode.parent < 0 ? glm::mat4(1.0f) : world[rNode.parent]) * (usePose ? rInstance.nodePose[i] : rNode.localTransform);
			for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes) rMeshes_.push_back({&rSubmesh.mesh, world[i]});
		}
	}
	else if(this->sourceEngine == STH2005_ONE)
	{
		const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(this->instanceId);
		const Graphics::RenderWareMesh& rMesh = Graphics::oneArchives[rInstance.archiveId].meshes[rInstance.meshIndex];

		/* The rest pose, like the bounds (a skinned atomic as if on its frame). */
		std::vector<glm::mat4> world(rMesh.frames.size());
		for(size_t i = 0; i < rMesh.frames.size(); i++) world[i] = (rMesh.frames[i].parent < 0 ? glm::mat4(1.0f) : world[rMesh.frames[i].parent]) * rMesh.frames[i].localTransform;
		for(const Graphics::RenderWareAtomic& rAtomic : rMesh.atomics)
		{
			for(const Graphics::RenderWareSubmesh& rSubmesh : rAtomic.submeshes) rMeshes_.push_back({&rSubmesh.mesh, world[rAtomic.frame]});
		}
	}
}

std::string Entity::instanceMeshTexture(const Graphics::BasicMesh* pMesh_) const
{
	std::string set = this->discTexture();
	if(set.empty()) return std::string();
	if(this->sourceEngine == SA_NINJA)
	{
		const Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
		const Graphics::TextureSet& rSet = Graphics::textureSets[rInstance.textureSetId];
		for(const Graphics::SANode& rNode : Graphics::saMeshes[rInstance.saMeshId].nodes)
		{
			for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes)
			{
				if(&rSubmesh.mesh != pMesh_) continue;
				if(rSubmesh.textureIndex == Graphics::SANoTexture || rSubmesh.textureIndex >= rSet.names.size()) return std::string();
				return std::format("disc:{}#{}", set, rSet.names[rSubmesh.textureIndex]);
			}
		}
	}
	else if(this->sourceEngine == STH2005_ONE)
	{
		const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(this->instanceId);
		for(const Graphics::RenderWareAtomic& rAtomic : Graphics::oneArchives[rInstance.archiveId].meshes[rInstance.meshIndex].atomics)
		{
			for(const Graphics::RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
			{
				if(&rSubmesh.mesh == pMesh_) return rSubmesh.textureName.empty() ? std::string() : std::format("disc:{}#{}", set, rSubmesh.textureName);
			}
		}
	}
	return std::string();
}

void Entity::drawInstance()
{
	if(this->pOverrideModel != nullptr)
	{
		Graphics::Texture* pTexture = this->sourceEngine == SA_NINJA ? Graphics::getSAInstance(this->instanceId).pOverrideTexture : Graphics::getONEInstance(this->instanceId).pOverrideTexture;
		PoolId shaderId = this->sourceEngine == SA_NINJA ? Graphics::getSAInstance(this->instanceId).shaderId : Graphics::getONEInstance(this->instanceId).shaderId;
		this->pOverrideModel->draw(shaderId, this->sourceEngine == STH2005_ONE, this->transform, pTexture);
		return;
	}
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).draw(this->transform);
		break;
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).draw(this->transform);
		break;
	/* External-only entities are drawn by Entity::draw; they have no instance. */
	case GLTF_FILE:
		break;
	}
}

/* Removes the entity's instance, which gives back its TextureSet or archive user. Called by PoolMap::remove. */
void Entity::destroyInstance()
{
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::saInstances.remove(this->instanceId);
		break;
	case STH2005_ONE:
		Graphics::oneInstances.remove(this->instanceId);
		break;
	case GLTF_FILE:
		break;
	}
}

void Entity::setSACollisionShapes(const SACollisionShapesKey& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addSAShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addSAShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setONECollisionShapes(const STH2005::CollisionKey& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}
