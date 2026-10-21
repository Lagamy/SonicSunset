#include "RenderStats.h"
#include "Scripting.h"
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include "Entity.h"
#include "GPU_API.h"
#include "API.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <format>

void Entity::checkNoInstance() const
{
	if(this->instanceId != UninitializedPoolId || this->isExternalOnly())
	{
		throw std::runtime_error("Entity: already has an instance or is external-only.");
	}
}

void Entity::setOverrideTexture(Graphics::Texture* pTexture_)
{
	if(this->isExternalOnly())
	{
		this->pExternalTexture = pTexture_;
		return;
	}
	this->setInstanceOverrideTexture(pTexture_);
}

void Entity::setExternalOnly(PoolId shaderId_)
{
	this->checkNoInstance();
	this->sourceEngine = GLTF_FILE;
	this->externalShaderId = shaderId_;
	this->refreshBounds();
}

void Entity::setOverrideModel(const Graphics::ExternalModel* pModel_)
{
	this->pOverrideModel = pModel_;
	this->refreshBounds();
}

void Entity::setPosition(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->previousPosition = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
	this->moveCameraModule();
	/* A teleport has no motion before it: the next move by the mean velocity (PhysicsBody::moveByMeanVelocity) starts from rest. */
	if(this->physicsId != UninitializedPoolId) Globals::Physics::bodies[this->physicsId].tickStartVelocity = glm::vec3(0.0f);
}

void Entity::moveTo(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
	this->moveCameraModule();
}

void Entity::moveCameraModule()
{
	if(this->cameraId == UninitializedPoolId) return;
	Camera& rCamera = Globals::Game::cameras[this->cameraId];
	if(rCamera.position != this->position) rCamera.set(this->position);
}

glm::vec3 Entity::renderPosition() const
{
	const GameplaySystem& rSystem = Globals::Game::gameplaySystem;
	if(rSystem.extrapolate)
	{
		/*
		A body on the floor is carried on by the move it actually made last tick, not by its velocity: a script that presses it into the floor (Shadow's ground gravity, about 0.2 units a tick) or keeps its velocity in the slope's plane
		sets a velocity the collision then cancels in part, and extrapolating by it sank the model into the floor and back every tick (observed in a log: velocity y of -48 against a move of -36 a second, and -11 at rest).
		*/
		glm::vec3 velocity = (this->position - this->previousPosition) * rSystem.tickRate;
		if(this->physicsId != UninitializedPoolId && !Globals::Physics::bodies[this->physicsId].grounded) velocity = Globals::Physics::bodies[this->physicsId].velocity;
		return this->position + velocity * (rSystem.interpolationAlpha() * rSystem.tickTime());
	}
	if(this->previousPosition == this->position) return this->position;
	return glm::mix(this->previousPosition, this->position, rSystem.interpolationAlpha());
}

/* The rotation the same way: the turn of the last tick carried on (extrapolation, t past 1) or the way between the last two orientations. Orientations are rotation matrices, so they go through quaternions. */
glm::mat4 Entity::renderRotation() const
{
	if(this->previousRotation == this->rotation) return this->rotation;
	const GameplaySystem& rSystem = Globals::Game::gameplaySystem;
	glm::quat from = glm::quat_cast(glm::mat3(this->previousRotation));
	glm::quat to = glm::quat_cast(glm::mat3(this->rotation));
	return glm::mat4_cast(glm::normalize(glm::slerp(from, to, (rSystem.extrapolate ? 1.0f : 0.0f) + rSystem.interpolationAlpha())));
}

void Entity::setRotation(const glm::mat4& rRotation_)
{
	this->rotation = rRotation_;
	this->previousRotation = rRotation_;
	this->transformDirty = true;
}

void Entity::turnTo(const glm::mat4& rRotation_)
{
	this->rotation = rRotation_;
	this->transformDirty = true;
}

void Entity::setScale(const glm::vec3& rScale_)
{
	this->scale = rScale_;
	this->transformDirty = true;
	this->refreshBounds();
}

void Entity::setModelMatrix(const glm::mat4& rMatrix_)
{
	this->modelMatrix = rMatrix_;
	this->transformDirty = true;
}

glm::vec3 Entity::collisionScale() const
{
	return this->physicsId == UninitializedPoolId ? this->scale : glm::vec3(1.0f);
}

void Entity::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;
	this->updateInstance(deltaTime_);
}

Graphics::BoundingSphere Entity::localBounds()
{
	Graphics::BoundingSphere bounds;
	if(this->isExternalOnly()) return this->pOverrideModel != nullptr ? this->pOverrideModel->bounds : bounds;
	if(this->instanceId == UninitializedPoolId) return bounds;
	if(this->pOverrideModel != nullptr) return this->pOverrideModel->bounds;
	return this->instanceBounds();
}

/* Whether the ray (mesh space; the direction is not renormalized, so t is the same parameter as in the world) crosses a triangle of the mesh placed by rWorld_, nearest t in rDistance_. A mesh not in the standard layout has no readable positions and counts as empty. */
bool rayHitsMesh(const Graphics::BasicMesh& rMesh_, const glm::mat4& rWorld_, const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rAny_)
{
	if(rMesh_.verticies.pLayout != &Graphics::VertexLayout::standard() || rMesh_.verticies.empty()) return false;
	glm::mat4 inverse = glm::inverse(rWorld_);
	glm::vec3 origin = glm::vec3(inverse * glm::vec4(rOrigin_, 1.0f));
	glm::vec3 direction = glm::vec3(inverse * glm::vec4(rDirection_, 0.0f));
	const Graphics::Vertex* pVertices = rMesh_.verticies.begin();
	size_t count = rMesh_.indicies.empty() ? rMesh_.verticies.size() : rMesh_.indicies.size();
	bool hit = false;
	rAny_ = true;

	/* Moller-Trumbore, both sides (the editor selects what it sees from behind too). */
	for(size_t i = 0; i + 2 < count; i += 3)
	{
		const glm::vec3& rA = pVertices[rMesh_.indicies.empty() ? i : rMesh_.indicies[i]].position;
		const glm::vec3& rB = pVertices[rMesh_.indicies.empty() ? i + 1 : rMesh_.indicies[i + 1]].position;
		const glm::vec3& rC = pVertices[rMesh_.indicies.empty() ? i + 2 : rMesh_.indicies[i + 2]].position;
		glm::vec3 edge1 = rB - rA;
		glm::vec3 edge2 = rC - rA;
		glm::vec3 p = glm::cross(direction, edge2);
		float determinant = glm::dot(edge1, p);
		if(std::abs(determinant) < 1e-12f) continue;
		float inverseDeterminant = 1.0f / determinant;
		glm::vec3 toOrigin = origin - rA;
		float u = glm::dot(toOrigin, p) * inverseDeterminant;
		if(u < 0.0f || u > 1.0f) continue;
		glm::vec3 q = glm::cross(toOrigin, edge1);
		float v = glm::dot(direction, q) * inverseDeterminant;
		if(v < 0.0f || u + v > 1.0f) continue;
		float t = glm::dot(edge2, q) * inverseDeterminant;
		if(t <= 0.0f) continue;
		if(!hit || t < rDistance_)
		{
			rDistance_ = t;
			hit = true;
		}
	}
	return hit;
}

bool Entity::rayHitsModel(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rHasTriangles_)
{
	rHasTriangles_ = false;
	if(this->scale.x == 0.0f || this->scale.y == 0.0f || this->scale.z == 0.0f) return false;
	if(!this->isExternalOnly() && this->instanceId == UninitializedPoolId) return false;

	/* The ray is taken into the model's own space by the inverse of the draw transform (the instance may turn its model, instanceTurn); the direction is not renormalized, so the distances stay world distances. */
	bool ofInstance = !this->isExternalOnly() && this->pOverrideModel == nullptr;
	glm::mat4 model = glm::translate(glm::mat4(1.0f), this->renderPosition()) * this->renderRotation() * glm::scale(glm::mat4(1.0f), this->scale);
	if(ofInstance) model = model * this->instanceTurn();
	glm::mat4 inverse = glm::inverse(model);
	glm::vec3 origin = glm::vec3(inverse * glm::vec4(rOrigin_, 1.0f));
	glm::vec3 direction = glm::vec3(inverse * glm::vec4(rDirection_, 0.0f));

	/* A rest pose box rejects cheaply before the triangles; a model with a pose built (it is animated) can reach out of its rest pose box, so it skips this. */
	bool posed = ofInstance && this->instancePosed();
	Graphics::BoundingSphere local = this->localBounds();
	if(local.boxValid && !posed)
	{
		/* Slab test: the ray is inside the box between the latest entry and the earliest exit of the three axis slabs. */
		float entry = 0.0f;
		float exit = 1e30f;
		for(int axis = 0; axis < 3; axis++)
		{
			if(std::abs(direction[axis]) < 1e-9f)
			{
				if(origin[axis] < local.boxLow[axis] || origin[axis] > local.boxHigh[axis]) { exit = -1.0f; break; }
				continue;
			}
			float t0 = (local.boxLow[axis] - origin[axis]) / direction[axis];
			float t1 = (local.boxHigh[axis] - origin[axis]) / direction[axis];
			entry = std::max(entry, std::min(t0, t1));
			exit = std::min(exit, std::max(t0, t1));
		}
		if(entry > exit)
		{
			/* A model with a box has triangles, so a miss of the box is a miss of the model (no sphere fallback). */
			rHasTriangles_ = true;
			return false;
		}
	}

	bool hit = false;
	float best = 0.0f;
	auto test = [&](const Graphics::BasicMesh& rMesh_, const glm::mat4& rWorld_)
	{
		float distance = 0.0f;
		if(rayHitsMesh(rMesh_, rWorld_, origin, direction, distance, rHasTriangles_) && (!hit || distance < best))
		{
			best = distance;
			hit = true;
		}
	};
	if(this->pOverrideModel != nullptr)
	{
		Graphics::ExternalPools& rPools = *this->pOverrideModel->pPools;
		for(const Graphics::ExternalSubmesh& rSubmesh : this->pOverrideModel->submeshes) test(rPools.meshes[rSubmesh.meshId], glm::mat4(1.0f));
	}
	else
	{
		std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>> meshes;
		this->instanceMeshes(meshes);
		for(const auto& [pMesh, rWorld] : meshes) test(*pMesh, rWorld);
	}
	if(hit) rDistance_ = best;
	return hit;
}

/* The smallest sphere around both (one inside the other gives the bigger). */
static Graphics::BoundingSphere enclose(const Graphics::BoundingSphere& rA_, const Graphics::BoundingSphere& rB_)
{
	float distance = glm::distance(rA_.center, rB_.center);
	if(distance + rB_.radius <= rA_.radius) return rA_;
	if(distance + rA_.radius <= rB_.radius) return rB_;
	Graphics::BoundingSphere result;
	result.radius = (distance + rA_.radius + rB_.radius) * 0.5f;
	result.center = rA_.center + (rB_.center - rA_.center) * ((result.radius - rA_.radius) / distance);
	result.valid = true;
	return result;
}

void Entity::refreshBounds()
{
	Graphics::BoundingSphere result = this->localBounds();
	result.valid = true;

	/* The index and withinRange place the sphere by position and rotation only, so the model's scale is applied here: the center per axis, the radius by the largest axis. */
	result.center *= this->scale;
	result.radius *= std::max({std::abs(this->scale.x), std::abs(this->scale.y), std::abs(this->scale.z)});
	if(this->collisionId != UninitializedPoolId)
	{
		Graphics::BoundingSphere shapes;
		shapes.valid = true;
		for(const Physics::CollisionShape& rSourceShape : Globals::Physics::collisions[this->collisionId].shapes)
		{
			Physics::CollisionShape rShape = Physics::scaledShape(rSourceShape, this->collisionScale(), usesNinjaShapes(this->sourceEngine));
			shapes.radius = std::max(shapes.radius, glm::length(rShape.center) + rShape.a + rShape.b + rShape.c);
		}
		result = enclose(result, shapes);
	}
	this->bounds = result;
	Globals::Game::spatialIndex.dirty = true;
}

bool Entity::withinRange(const glm::vec3& rCameraPosition_, float maxDistance_)
{
	glm::vec3 center = this->position + glm::mat3(this->rotation) * this->bounds.center;
	return glm::distance(center, rCameraPosition_) - this->bounds.radius <= maxDistance_;
}

/*
Draws the entity unless it is out of sight: the model's bounding sphere (from its rest pose, with a margin) is moved by the entity's transform (its radius by the largest scale) and tested against the view frustum and against maxDistance_
from the camera (both skipped with skipCulling_: the editor's selected entities). The test uses the transform of the frame that is drawn (renderPosition and renderRotation).
*/
void Entity::draw(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_, bool skipCulling_)
{
	if(this->instanceId == UninitializedPoolId && !(this->isExternalOnly() && this->pOverrideModel != nullptr)) return;
	if(!levelShown(this->levelId)) return;
	glm::vec3 renderPosition = this->renderPosition();
	glm::mat4 renderRotation = this->renderRotation();
	if(this->transformDirty || renderPosition != this->renderedPosition || renderRotation != this->renderedRotation)
	{
		this->transform = glm::translate(glm::mat4(1.0f), renderPosition) * renderRotation * glm::scale(glm::mat4(1.0f), this->scale) * this->modelMatrix;
		this->renderedPosition = renderPosition;
		this->renderedRotation = renderRotation;
		this->transformDirty = false;
	}

	Graphics::BoundingSphere bounds = this->localBounds();
	glm::vec3 center = glm::vec3(this->transform * glm::vec4(bounds.center, 1.0f));
	float radius = bounds.radius * std::max({std::abs(this->scale.x), std::abs(this->scale.y), std::abs(this->scale.z)});
	if(!skipCulling_ && glm::distance(center, rCameraPosition_) - radius > maxDistance_) return;
	if(!skipCulling_ && !sphereInFrustum(rPlanes_, center, radius)) return;

	Graphics::renderStats.entitiesDrawn++;
	if(this->isExternalOnly())
	{
		this->pOverrideModel->draw(this->externalShaderId, false, this->transform, this->pExternalTexture);
		return;
	}
	this->drawInstance();
}

void Entity::setPhysics(float weight_)
{
	if(this->physicsId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a physics.");
	}
	this->physicsId = Globals::Physics::bodies.add(Physics::PhysicsBody{weight_});

	/* The collision shapes of an entity with a physics body are not scaled (collisionScale), which changes its bounds. */
	this->refreshBounds();
}

void Entity::markForDestroy()
{
	Globals::Game::markedForDestroy.add(this->selfId);
}

void Entity::destroy()
{
	if(this->scriptStateRef != 0)
	{
		Scripting::releaseState(this->scriptStateRef);
		this->scriptStateRef = 0;
	}
	if(this->cameraId != UninitializedPoolId)
	{
		bool wasMain = Globals::Game::mainCamera == this->cameraId;
		if(wasMain) Globals::Game::mainCamera = UninitializedPoolId;
		Globals::Game::cameras.remove(this->cameraId);

		/* The game always needs a main camera (getMainCamera throws without): the first camera that is left and renders to the screen takes over. */
		if(wasMain)
		{
			for(uint32_t i = 0; i < Globals::Game::cameras.objects.size(); i++)
			{
				if(Globals::Game::cameras.alive[i] && Globals::Game::cameras.objects[i].renderTarget() == nullptr)
				{
					Globals::Game::mainCamera = PoolId(i, Globals::Game::cameras.generation[i]);
					break;
				}
			}
		}
		this->cameraId = UninitializedPoolId;
	}
	if(this->stateId != UninitializedPoolId)
	{
		Globals::Game::entityStates.remove(this->stateId);
		this->stateId = UninitializedPoolId;
	}
	if(this->behaviorSetId != UninitializedPoolId)
	{
		Globals::Game::entityBehaviors.remove(this->behaviorSetId);
		this->behaviorSetId = UninitializedPoolId;
	}
	if(this->physicsId != UninitializedPoolId)
	{
		Globals::Physics::bodies.remove(this->physicsId);
		this->physicsId = UninitializedPoolId;
	}
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
	}
	if(this->instanceId == UninitializedPoolId) return;
	this->destroyInstance();
	this->instanceId = UninitializedPoolId;
}
