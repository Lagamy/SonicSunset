#pragma once
#include "GPU_API.h"
#include "GameEntity.h"
#include "PoolId.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "ExternalField.h"
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "Constants.h"

namespace Graphics
{
struct ExternalModel;
}
#include "BoundingSphere.h"
#include <glm/gtc/matrix_transform.hpp>

/*
An entity is created first and gets its instance afterwards, with the setters of the game (Game/GameEntityMembers.inl: setSAGPUInstance, setONEGPUInstance ...); an entity has a single instance. Until then instanceId is UninitializedPoolId and the entity is not drawn.
sourceEngine (SourceEngineEnum, the game's: Game/GameEntity.h) says which engine's instance pool instanceId refers to.
Removing the entity from its pool (destroy) removes the instance, its collision and its physics.
physicsId is the PoolId of the entity's Physics::PhysicsBody in Globals::Physics::bodies (UninitializedPoolId = none, the entity is then not moved by the physics and collision systems); setPhysics creates it.
stagingPosition is where the physics system wants the entity to go this frame: the collision system checks it, may change it, and then sets position to it (see PhysicsSystem.h, CollisionSystem.h).
The new fields come after the position ones so Entity{engine, instanceId, position} (API.cpp) still initializes position.
collisionId is the PoolId of the entity's collision in Globals::Physics::collisions (UninitializedPoolId = none); the entity owns it.
The game's collision setters (setSACollisionShapes, setONECollisionShapes) give it its shapes (Physics::CollisionShape); the collision system moves an entity by its sphere (form 0) and capsule (form 6) shapes, and by a segment if it has none of those; the lines view draws them all.
selfId is the entity's own PoolId in Globals::Game::entities (set by addEntity), so the entity can hand it to what needs it: its collision (Collision::entityId) and markForDestroy.
stateId is its EntityState in Globals::Game::entityStates (UninitializedPoolId = none; made by entityStateOf in API.h, removed by destroy).
behaviorSetId is the entity's record in Globals::Game::entityBehaviors (UninitializedPoolId = no behaviours): its behaviours, stacked in the order they were given and run in that order by the GameplaySystem every gameplay tick; add one with giveEntityBehavior (API.h), which makes the record. destroy removes it.
cameraId is the entity's camera module, its Camera in Globals::Game::cameras (UninitializedPoolId = none; made by addCamera in API.h, removed by destroy). The camera is at the entity's position: setPosition and moveTo give it to the camera (moveCameraModule); the view direction is the camera's own. Set the position of a camera through its entity (the follow camera does), not with Camera::set alone, or the entity is left behind.
previousPosition and previousRotation are where the entity was at the start of the latest gameplay tick. What is drawn (and shown by the debug views) is renderPosition and renderRotation, by how far the frame is
into the next tick (GameplaySystem::interpolationAlpha): extrapolated from the latest tick by the entity's velocity (a physics entity's body, otherwise the last tick's change) so that it is not a tick behind, or, with
GameplaySystem::extrapolate off, interpolated between the last two ticks. The rotation is turned the same way between the two orientations (a quaternion slerp). setPosition and setRotation place the entity (a
teleport: previous and current are the same); moveTo and turnTo are the simulation's moves (physics, collisions, behaviours) and keep the previous ones.
The matrix of position, rotation and scale (translate * rotation * scale; rotation is a world-space orientation matrix, identity by default, set with setRotation: the caller decides the convention; scale is per axis of the entity's own space, (1, 1, 1) by default, set with setScale) is kept in transform and rebuilt by draw only after the position, rotation or scale changed: setPosition, setRotation and setScale mark it
dirty (a new entity starts dirty). Change them with the setters, not by writing the fields.
The model and the collision shapes of an entity without a physics body are scaled (collisionScale, Physics::scaledShape: the port's choice, whether the game's objects scale their collision is unknown); the physics body's shapes and the camera module keep their size. bounds holds the scaled model sphere.
*/
struct Entity
{
	SourceEngineEnum sourceEngine = DefaultSourceEngine;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;
	glm::vec3 previousPosition = glm::vec3(0.0f);
	glm::vec3 renderedPosition = glm::vec3(0.0f);
	glm::mat4 previousRotation = glm::mat4(1.0f);
	glm::mat4 renderedRotation = glm::mat4(1.0f);
	glm::mat4 transform;
	bool transformDirty = true;
	/* Set every gameplay tick (SpatialIndex::updateActive): true while the entity is in range of the camera, false otherwise and until the first tick; an entity that is not active has no behaviours, physics or collision. */
	bool active = false;
	/* An entity with this set is never put to sleep by distance: its behaviours, physics and collision run from any distance (the player, things that must keep going). */
	bool behaviorNotCulled = false;
	PoolId collisionId = UninitializedPoolId;
	PoolId physicsId = UninitializedPoolId;
	glm::vec3 stagingPosition = glm::vec3(0.0f);
	glm::mat4 rotation = glm::mat4(1.0f);
	glm::vec3 scale = glm::vec3(1.0f);
	/* A draw-only matrix applied after the scale, in the entity's own space (identity = none): a character's temporary deformation (setModelMatrix). Not part of the bounds, the picking or the collision. */
	glm::mat4 modelMatrix = glm::mat4(1.0f);
	PoolId behaviorSetId = UninitializedPoolId;
	PoolId selfId = UninitializedPoolId;
	PoolId stateId = UninitializedPoolId;
	PoolId cameraId = UninitializedPoolId;
	/* The prefab the entity was made from (Globals::Game::prefabs, set by spawnPrefab; UninitializedPoolId = none, e.g. a camera): the editor groups the entity list by it. */
	PoolId prefabId = UninitializedPoolId;
	/* The level the entity belongs to (Globals::Game::levels; set by spawnPrefab from SpawnParams::levelId, UninitializedPoolId = none: players, script spawns, cameras): its prefab edits are the level's own sets (Prefab::inLevels); without one the active level's apply. */
	PoolId levelId = UninitializedPoolId;
	/* The HubWorld owns the entity (HubWorld::entityIds, HubWorld.json "entities"), not a level: levelId stays none. The editor lists it in the HubWorld tab only. */
	bool hubWorldOwned = false;
	/* The texture the editor gave the entity in place of its model's own (setEntityTexture in API.h; Default = none). defaultTexture is the model's own disc texture (discTexture), remembered by the first change so Default can put it back; hasDefaultTexture says it was. */
	TextureChoice textureChoice;
	std::string defaultTexture;
	bool hasDefaultTexture = false;
	/* The same for the model (setEntityMesh in API.h): the choice, the model's own disc mesh (discMesh) remembered by the first change, and the external model drawn in place of the instance's (setOverrideModel; nullptr = the instance's). */
	MeshChoice meshChoice;
	std::string defaultMesh;
	bool hasDefaultMesh = false;
	const Graphics::ExternalModel* pOverrideModel = nullptr;
	/* External-only entity (setExternalOnly; sourceEngine GLTF_FILE): the shader it is drawn with (the GLTF one) and the external texture it takes, in place of the instance's shader and pOverrideTexture it does not have. */
	PoolId externalShaderId = UninitializedPoolId;
	Graphics::Texture* pExternalTexture = nullptr;
	/* The entity's table for what its script behaviours remember (sf.state, Scripting.h): a Lua registry reference, 0 = none yet; released by destroy. */
	int scriptStateRef = 0;
	/* Set when a script changed a value in the state table, at any depth (the table is a tracked proxy, Scripting.cpp); the fields written and what a behaviour does on its first run on the entity (its setup) do not set it. The level reset (5c) restores only entities with it set. */
	bool scriptStateDirty = false;
	/* The values the editor set for the external fields (ExternalField.h) of the entity's script behaviours: behaviour name -> field name -> value; a field not in it has its declared default. externalsApplied lists the behaviours Scripting::callBehavior has already run on the entity (their fields are written into the state table at that first run). */
	std::map<std::string, std::map<std::string, ExternalValue>> externals;
	std::vector<PoolId> externalsApplied;
	/* Values of the level file that no declared field takes (the behaviour or field is not declared when the level loads, or the value does not fit its type), as their JSON text, behaviour name -> field name: kept so that saving the level does not lose them (Loading::saveLevelEdits writes them back). */
	std::map<std::string, std::map<std::string, std::string>> externalsKept;
	/* The members that are the game's: the motion it wants and plays, and the setters of its instance and collision shapes. */
#include "GameEntityMembers.inl"


	/* The members below are defined by the game (Game/GameEntity.cpp), since they depend on the engine of the instance (disc* are what the editor shows of the instance's disc data). */
	/* The disc texture the instance draws with: an SA instance's TextureSet (its .gvm path), a ONE instance's .TXD entry name; empty = untextured. Throws without an instance. */
	std::string discTexture() const;
	/* Draws with another disc texture (empty = untextured; Loading::changeSATextureSet / changeActiveTexture, which throw when it can't, leaving the entity as it was). A ONE name is an entry of the instance's own archive. */
	void setDiscTexture(const std::string& rName_);
	/* The .TXD entries of the instance's archive, for a ONE instance (empty for an SA one). */
	std::vector<std::string> discTextureOptions() const;
	/* The disc model the instance draws: an SA instance's SAMesh name ("<rel>@0x<offset>"), a ONE instance's mesh name in its archive. Throws without an instance. */
	std::string discMesh() const;
	/*
	Draws another disc model: an SA name is the form discMesh gives (Loading::changeSAMesh: the entity's texture set must hold the texture ids the model uses; a motion in progress is dropped and gameplay has to ask for one again), a ONE name is a mesh the archive
	has built already (the entity's own) or "(mesh)/<entry>" for a .DFF entry of the archive, built as a static model. Throws when it can't, leaving the entity as it was.
	*/
	void setDiscMesh(const std::string& rName_);
	/* The .DFF entries of the instance's archive, for a ONE instance (empty for an SA one). */
	std::vector<std::string> discMeshOptions() const;
	/* Advances the instance by a frame: plays the wanted animation (animationIntent), then updates the instance. Called by update when the entity has an instance. */
	void updateInstance(float deltaTime_);
	/* The bounding sphere of the instance's model in its own space, with the engine's own turn applied (instanceTurn); computed once per mesh. Without an instance or for an engine that has none, an invalid one. */
	Graphics::BoundingSphere instanceBounds();
	/* Editor picking (rayHitsModel): the turn the engine gives the model before the entity's transform (identity for most), whether the instance's meshes are posed (animated, so its rest pose box can't reject a ray), and its meshes with their placement in the model's space. */
	glm::mat4 instanceTurn() const;
	bool instancePosed() const;
	void instanceMeshes(std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>>& rMeshes_) const;
	/* Draws the instance (or, with pOverrideModel set, the external model with the instance's shader and texture) at transform. Called by draw once the entity passed the culling. */
	void drawInstance();
	/* Removes the instance from its pool. Called by destroy. */
	void destroyInstance();
	/* Sets the external texture the instance draws with in place of its own. */
	void setInstanceOverrideTexture(Graphics::Texture* pTexture_);
	/* Throws if the entity already has an instance or is external-only (for the setters that give one). */
	void checkNoInstance() const;
	/*
	Makes the entity one without a disc instance: it is drawn only by an external model (setOverrideModel, nothing is drawn until one is set) with an external texture (setOverrideTexture) and this shader (the GLTF one: unlit, linear vertex colour, as the
	external models are written), independent of SADX and Shadow data. sourceEngine becomes GLTF_FILE. Throws if the entity already has an instance. It has no disc model or texture: setEntityMesh / setEntityTexture refuse a Disc choice for it.
	*/
	void setExternalOnly(PoolId shaderId_);
	bool isExternalOnly() const { return this->sourceEngine == GLTF_FILE; }
	/* Whether setEntityMesh and setEntityTexture have something to change: an instance, or the external-only drawing. */
	bool hasModelSlot() const { return this->instanceId != UninitializedPoolId || this->isExternalOnly(); }
	/* Draws this external model in place of the instance's (nullptr = the instance's) and refreshes the bounds. The model is not owned: it has to stay alive while set (the external pools do). */
	void setOverrideModel(const Graphics::ExternalModel* pModel_);
	/* Draws every textured submesh with this texture (nullptr = the instance's own textures). The texture is not owned: it has to stay alive while set (the external pools do). */
	void setOverrideTexture(Graphics::Texture* pTexture_);
	void setPhysics(float weight_);
	void setPosition(const glm::vec3& rPosition_);
	void moveTo(const glm::vec3& rPosition_);
	/* Puts the camera module (if any) at the entity's position. */
	void moveCameraModule();
	glm::vec3 renderPosition() const;
	void setRotation(const glm::mat4& rRotation_);
	void turnTo(const glm::mat4& rRotation_);
	glm::mat4 renderRotation() const;
	/* Sets the scale of the model (see above) and refreshes the bounds. */
	void setScale(const glm::vec3& rScale_);
	/* Sets the draw-only matrix applied after the scale (see modelMatrix). */
	void setModelMatrix(const glm::mat4& rMatrix_);
	/* The scale the entity's collision shapes get in the contacts and the lines view: its scale, or (1, 1, 1) for an entity with a physics body. */
	glm::vec3 collisionScale() const;
	/* Advances the entity's instance (updateInstance); nothing without one. */
	void update(float deltaTime_);
	/* The bounding sphere of the entity's model in its own space, with the engine's own turn applied (invalid without an instance); computed once per mesh. Used to draw. */
	Graphics::BoundingSphere localBounds();

	/* Editor picking: whether the ray (world space, unit direction) crosses a triangle of the model (both sides; an SA model as posed in the last drawn frame, a ONE model in its rest pose); distance_ is along the ray to the nearest one. hasTriangles_ is false when the model has none readable (no model, or a custom vertex layout), then the result is false and the caller falls back to bounds. */
	bool rayHitsModel(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rHasTriangles_);
	/*
	The sphere the spatial index uses for the entity (what is in range of the camera and so active), in its own space: the model's sphere and a sphere around the collision shapes (about the origin, a conservative
	|center| + a + b + c each, so it holds whatever the rotation), enclosed in one; a point if it has neither. refreshBounds recomputes it and tells the index to rebuild: every function that attaches or changes a model,
	a collision or a physics calls it, code that sets collisionId by hand has to call it too.
	*/
	Graphics::BoundingSphere bounds;
	void refreshBounds();
	/* Whether the entity's bounding sphere, at the position and rotation of the latest gameplay tick, is within maxDistance_ of the camera: the distance half of the draw culling, for the behaviours. */
	bool withinRange(const glm::vec3& rCameraPosition_, float maxDistance_);
	void draw(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_, bool skipCulling_ = false);
	void destroy();

	/* Asks for the entity to be removed: it goes into Globals::Game::markedForDestroy and the GameplaySystem removes it (destroy runs then) at the start of the next gameplay tick, so a behaviour or a callback can end its own entity safely. */
	void markForDestroy();
};

/* Whether the ray (in the mesh's space, direction not renormalized) crosses a triangle of the mesh placed by rWorld_ (both sides), nearest t in rDistance_; rAny_ is set once the mesh has readable triangles. Shared by Entity::rayHitsModel and Level::rayHitsInstance. */
bool rayHitsMesh(const Graphics::BasicMesh& rMesh_, const glm::mat4& rWorld_, const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rAny_);
