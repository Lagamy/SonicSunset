#include "API.h"
#include "Entity.h"
#include "Level.h"
#include "Scripting.h"
#include "GamePrefab.h"
#include "ExternalAssets.h"
#include "GPU_API.h"
#ifdef EDITOR
#include "Editor.h"
#endif
#include <algorithm>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <utility>

/* Adds the entity to the pool given (the game's by default). Only an entity of the game's pool goes into the spatial index; the editor's own pools (PrefabThumbnails) are never seen by the gameplay or the renderer's loops. */
PoolId addEntity(const char* name_, glm::vec3 position_, PoolMap<Entity>* pPool_)
{
    PoolId id = pPool_->add(name_, Entity{DefaultSourceEngine, UninitializedPoolId, position_});
    (*pPool_)[id].selfId = id;
    (*pPool_)[id].previousPosition = position_;
    if(pPool_ == &Globals::Game::entities) Globals::Game::spatialIndex.addDynamic(id);
    return id;
}

/* The reference stays valid while the entity does (the pool does not move its entities when it grows): once it is removed the slot can be reused, so keep the PoolId across anything that removes entities. */
Entity& getEntity(PoolId id_)
{
    return Globals::Game::entities[id_];
}

/*
The entity added under that name (the first one if a name was used twice). Throws if there is none, in every build: PoolMap::getIdByName
only checks in Debug builds. Same lifetime warning as getEntity.
*/
Entity& getEntityByName(const char* name_)
{
    auto found = Globals::Game::entities.nameToId.find(name_);
    if(found == Globals::Game::entities.nameToId.end())
    {
        throw std::runtime_error(std::format("getEntityByName: no entity named {}.", name_));
    }
    return Globals::Game::entities[found->second];
}

PoolId addLevel(const char* name_, glm::vec3 position_)
{
    PoolId id = Globals::Game::levels.add(name_, Level{DefaultSourceEngine, UninitializedPoolId, position_});
    Globals::Game::levels[id].selfId = id;
    return id;
}

PoolId activeLevelId()
{
    /* The level the HubWorld names; none loaded under that name (a level made another way) = the lowest alive slot. */
    if(!Globals::Game::hubWorld.active.empty())
    {
        for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
        {
            if(Globals::Game::levels.alive[i] && Globals::Game::levels.names[i] == Globals::Game::hubWorld.active) return PoolId(i, Globals::Game::levels.generation[i]);
        }
    }
    for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
    {
        if(Globals::Game::levels.alive[i]) return PoolId(i, Globals::Game::levels.generation[i]);
    }
    return UninitializedPoolId;
}

bool levelShown(PoolId levelId_)
{
#ifdef EDITOR
    if(!editor.active || editor.hubWorldView || editor.hubWorldEntities || levelId_ == UninitializedPoolId) return true;
    return levelId_ == activeLevelId();
#else
    return true;
#endif
}

/* Adds a collision tag (a category, see BATag) under a name. */
PoolId addBATag(const BATagKey& rKey_)
{
    return Globals::Game::baBehaviors.addTag(rKey_);
}

/* Adds a collision behaviour (a bag of tags and rules, see BABehavior) under its key's name. */
PoolId addBehavior(const BABehaviorKey& rKey_, BABehavior behavior_)
{
    return Globals::Game::baBehaviors.add(rKey_, std::move(behavior_));
}

/* True if the id names a live entity (its slot is alive and the generation matches), for code that holds an id across things that can remove entities. */
bool entityExists(PoolId entityId_)
{
    return entityId_.id < Globals::Game::entities.alive.size() && Globals::Game::entities.alive[entityId_.id] && Globals::Game::entities.generation[entityId_.id] == entityId_.generation;
}

/* Posts a one-shot presentation event (see GameEvents) and registers what handles one by name. */
void postGameEvent(const char* name_, PoolId entityId_, const glm::vec3& rPosition_)
{
    Globals::Game::gameEvents.post(name_, entityId_, rPosition_);
}

void addGameEventHandler(const char* name_, GameEventHandler pHandler_)
{
    Globals::Game::gameEvents.addHandler(name_, pHandler_);
}

/* The state of an entity's behaviours (EntityState.h), made on the first call. The reference stays valid until the entity is removed. */
EntityState& entityStateOf(PoolId entityId_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.stateId == UninitializedPoolId)
    {
        rEntity.stateId = Globals::Game::entityStates.add(EntityState{});
    }
    return Globals::Game::entityStates[rEntity.stateId];
}

/* Adds an entity behaviour under a name (see EntityBehavior); find it again with Globals::Game::entityBehaviorTypes.nameToId. */
PoolId addEntityBehavior(const char* name_, void (*pBehave_)(PoolId entityId_))
{
    return Globals::Game::entityBehaviorTypes.add(name_, EntityBehavior{pBehave_});
}

/* Stacks a behaviour on an entity (after the ones it has); its first one makes the entity's record in Globals::Game::entityBehaviors, which the GameplaySystem walks. */
void giveEntityBehavior(PoolId entityId_, PoolId behaviorId_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.behaviorSetId == UninitializedPoolId)
    {
        rEntity.behaviorSetId = Globals::Game::entityBehaviors.add(EntityBehaviorSet{entityId_, {}});
    }
    Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds.push_back(behaviorId_);
}

/* Takes the first copy of the behaviour off the entity (the others stay, a behaviour can be stacked twice); false if it has none. The entity keeps its record when the last one goes. The GameplaySystem works on copies of its lists, so this is safe from a behaviour. */
bool removeEntityBehavior(PoolId entityId_, PoolId behaviorId_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.behaviorSetId == UninitializedPoolId) return false;
    std::vector<PoolId>& rIds = Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds;
    auto found = std::find(rIds.begin(), rIds.end(), behaviorId_);
    if(found == rIds.end()) return false;
    rIds.erase(found);
    return true;
}

/* Replaces the entity's behaviours with exactly these, in this order (the editor's save, restore and Stop put an entity's list back); an entity with none and nothing to give keeps no record. */
void setEntityBehaviors(PoolId entityId_, const std::vector<PoolId>& rBehaviorIds_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.behaviorSetId == UninitializedPoolId)
    {
        if(rBehaviorIds_.empty()) return;
        rEntity.behaviorSetId = Globals::Game::entityBehaviors.add(EntityBehaviorSet{entityId_, {}});
    }
    Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds = rBehaviorIds_;
}

/* The shapes of the entity's collision, empty if it has none. */
std::vector<Physics::CollisionShape> entityShapes(PoolId entityId_)
{
    const Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.collisionId == UninitializedPoolId) return {};
    return Globals::Physics::collisions[rEntity.collisionId].shapes;
}

/* Replaces the shapes of the entity's collision with exactly these (the editor's bound area edits, save, restore and Stop); an entity with no collision gets a hand-made one for a non-empty list, and none for an empty one. The bounds are refreshed. */
void setEntityShapes(PoolId entityId_, const std::vector<Physics::CollisionShape>& rShapes_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.collisionId == UninitializedPoolId)
    {
        if(rShapes_.empty()) return;
        Physics::Collision collision;
        collision.name = "(edited)";
        collision.entityId = entityId_;
        rEntity.collisionId = Globals::Physics::collisions.add(std::move(collision));
    }
    Globals::Physics::collisions[rEntity.collisionId].shapes = rShapes_;
    rEntity.refreshBounds();
}

/* The texture the editor gave the entity (Default = its model's own). */
TextureChoice entityTexture(PoolId entityId_)
{
    return Globals::Game::entities[entityId_].textureChoice;
}

/*
Draws the entity with the texture of the choice (TextureChoice.h): Default puts the model's own back (remembered by the first change), Disc loads that disc texture for the instance, External draws the external texture of that name on every textured
submesh. Throws std::runtime_error, leaving the entity as it was, when the entity has no model, the external texture does not exist, or the disc texture can't be loaded or does not fit the model.
*/
void setEntityTexture(PoolId entityId_, const TextureChoice& rChoice_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(!rEntity.hasModelSlot()) throw std::runtime_error("the entity has no model");
    if(rEntity.isExternalOnly() && rChoice_.kind == TextureChoiceKind::Disc) throw std::runtime_error("an external-only entity has no disc textures");
    if(!rEntity.isExternalOnly() && !rEntity.hasDefaultTexture)
    {
        rEntity.defaultTexture = rEntity.discTexture();
        rEntity.hasDefaultTexture = true;
    }
    Graphics::Texture* pExternal = nullptr;
    if(rChoice_.kind == TextureChoiceKind::External)
    {
        pExternal = Loading::findExternalTexture(rChoice_.name);
        if(pExternal == nullptr) throw std::runtime_error(std::format("there is no external texture named {}", rChoice_.name));
    }
    if(!rEntity.isExternalOnly()) rEntity.setDiscTexture(rChoice_.kind == TextureChoiceKind::Disc ? rChoice_.name : rEntity.defaultTexture);
    rEntity.setOverrideTexture(pExternal);
    rEntity.textureChoice = rChoice_;
}

/* The model the editor gave the entity (Default = its own). */
MeshChoice entityMesh(PoolId entityId_)
{
    return Globals::Game::entities[entityId_].meshChoice;
}

/*
Draws the entity with the model of the choice (MeshChoice.h): Default puts the entity's own back (remembered by the first change), Disc loads that disc model into the instance (the entity's texture set and dictionary stay), External draws the external model of
that name in place of the instance's. Throws std::runtime_error, leaving the entity as it was, when the entity has no model, the external model does not exist, or the disc model can't be loaded or does not fit the entity's textures.
*/
void setEntityMesh(PoolId entityId_, const MeshChoice& rChoice_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(!rEntity.hasModelSlot()) throw std::runtime_error("the entity has no model");
    if(rEntity.isExternalOnly() && rChoice_.kind == MeshChoiceKind::Disc) throw std::runtime_error("an external-only entity has no disc models");
    if(!rEntity.isExternalOnly() && !rEntity.hasDefaultMesh)
    {
        rEntity.defaultMesh = rEntity.discMesh();
        rEntity.hasDefaultMesh = true;
    }
    const Graphics::ExternalModel* pExternal = nullptr;
    if(rChoice_.kind == MeshChoiceKind::External)
    {
        pExternal = Loading::findExternalModel(rChoice_.name);
        if(pExternal == nullptr) throw std::runtime_error(std::format("there is no external model named {}", rChoice_.name));
    }
    if(!rEntity.isExternalOnly())
    {
        std::string disc = rEntity.defaultMesh;
        if(rChoice_.kind == MeshChoiceKind::Disc) disc = gameDiscMeshName(rEntity.sourceEngine, rChoice_.name);
        rEntity.setDiscMesh(disc);
    }
    rEntity.setOverrideModel(pExternal);
    rEntity.meshChoice = rChoice_;
}

/* Moves the main camera (Globals::Game::mainCamera) through its entity, which carries it (Entity::moveCameraModule; the view and its UBO are updated by Camera::set); a camera made by hand has no entity and is moved itself. */
void setCameraPosition(const glm::vec3& rPosition_)
{
    Camera& rCamera = getMainCamera();
    if(entityExists(rCamera.entityId)) getEntity(rCamera.entityId).setPosition(rPosition_);
    else rCamera.set(rPosition_);
}

/*
Adds a camera as the module of a new entity of that name (Entity::cameraId), looking where Camera looks by default, rendering to the screen. The first one made becomes the main camera. Returns the entity's PoolId; its
camera is getEntity(id).cameraId in Globals::Game::cameras. Removing the entity removes the camera.
*/
PoolId addCamera(const char* name_, const glm::vec3& rPosition_)
{
    PoolId entityId = addEntity(name_, rPosition_);
    PoolId cameraId = Globals::Game::cameras.add(rPosition_);
    Globals::Game::cameras[cameraId].entityId = entityId;
    Globals::Game::entities[entityId].cameraId = cameraId;
    if(Globals::Game::mainCamera == UninitializedPoolId) Globals::Game::mainCamera = cameraId;
    return entityId;
}

/* The camera the player looks through (Globals::Game::mainCamera); throws if there is none. */
Camera& getMainCamera()
{
    if(Globals::Game::mainCamera == UninitializedPoolId)
    {
        throw std::runtime_error("getMainCamera: there is no main camera.");
    }
    return Globals::Game::cameras[Globals::Game::mainCamera];
}

/* Adds a render target of that size in pixels (RenderTarget.h) for Camera::setRenderTarget. */
PoolId addRenderTarget(uint32_t width_, uint32_t height_)
{
    return Globals::Game::renderTargets.add(width_, height_);
}

/* Same lifetime as getEntity: valid while the level is. */
Level& getLevel(PoolId id_)
{
    return Globals::Game::levels[id_];
}

/* Like getEntityByName: the first level added under that name, throws if there is none. */
Level& getLevelByName(const char* name_)
{
    auto found = Globals::Game::levels.nameToId.find(name_);
    if(found == Globals::Game::levels.nameToId.end())
    {
        throw std::runtime_error(std::format("getLevelByName: no level named {}.", name_));
    }
    return Globals::Game::levels[found->second];
}


/*
Makes one entity from a prefab at any time (the level's SET and placement records, a script's spawnPlayer, the editor): an entity named rParams_.name (the prefab's name if empty) at the position and rotation given, with Entity::prefabId set, built as the
prefab's kind says (buildSAObject, buildONEObject; the players are Sonic's and Shadow's model, idle motions, body shape (their Changed file's) and physics of weight 1, which the script then attaches with attachPlayer), and the behaviours the script registered for
the prefab (sf.prefabBehavior, called with rParams_.pPlacement, its SET record, null for an object that has none). Returns its id, or UninitializedPoolId if it could not be made: the entity is removed again and the reason, with rParams_.what naming where it came from,
is printed on stderr, as for an object of a level. The entity is not owned by anything: whoever asks for it keeps its id.
pPool_ is the pool the entity is made in (the game's by default). In any other pool (the editor's PrefabThumbnails) the entity is only what is drawn: its model, texture and motion. No collision, physics, script behaviour or spatial index entry, since the
collision and physics pools, the scripts and the index all name entities by their id in the game's pool; the spatial index is also left as it was (the model setters mark it dirty).
*/
PoolId spawnPrefab(PoolId prefabId_, PoolId shaderId_, const SpawnParams& rParams_, PoolMap<Entity>* pPool_)
{
    const bool inScene = pPool_ == &Globals::Game::entities;
    const bool indexWasDirty = Globals::Game::spatialIndex.dirty;
    if(prefabId_ == UninitializedPoolId)
    {
        std::fprintf(stderr, "Spawn: skipped %s %s: there is no prefab for it.\n", rParams_.name.c_str(), rParams_.what.c_str());
        return UninitializedPoolId;
    }
    const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
    std::string name = rParams_.name.empty() ? rPrefab.name : rParams_.name;
    PoolId id = addEntity(name.c_str(), rParams_.position, pPool_);
    try
    {
        Entity& rEntity = (*pPool_)[id];
        rEntity.prefabId = prefabId_;
        rEntity.levelId = rParams_.levelId;
        rEntity.setRotation(rParams_.rotation);
        switch(rPrefab.kind)
        {
        /* The model and texture are the prefab's definition, so they are applied in every pool (the editor's thumbnails draw them too), not only in the scene. */
        case PrefabKind::External:
            rEntity.setExternalOnly(Graphics::getShaderId("GLTF"));
            if(inScene) Scripting::setupPrefab(rEntity.selfId, Globals::Game::prefabs.names[prefabId_.id].c_str(), nullptr);
            /* Set on the entity itself, not through setEntityMesh / setEntityTexture, which name the game's pool (this may be the thumbnails'). A missing asset is reported and the entity is made without it. */
            if(rPrefab.definition.mesh.kind == MeshChoiceKind::External)
            {
                const Graphics::ExternalModel* pModel = Loading::findExternalModel(rPrefab.definition.mesh.name);
                if(pModel != nullptr)
                {
                    rEntity.setOverrideModel(pModel);
                    rEntity.meshChoice = rPrefab.definition.mesh;
                }
                else std::fprintf(stderr, "Spawn: %s %s: there is no external model named %s\n", rPrefab.name.c_str(), rParams_.what.c_str(), rPrefab.definition.mesh.name.c_str());
            }
            if(rPrefab.definition.texture.kind == TextureChoiceKind::External)
            {
                Graphics::Texture* pTexture = Loading::findExternalTexture(rPrefab.definition.texture.name);
                if(pTexture != nullptr)
                {
                    rEntity.setOverrideTexture(pTexture);
                    rEntity.textureChoice = rPrefab.definition.texture;
                }
                else std::fprintf(stderr, "Spawn: %s %s: there is no external texture named %s\n", rPrefab.name.c_str(), rParams_.what.c_str(), rPrefab.definition.texture.name.c_str());
            }
            break;
        /* The prefabs of the game's own kinds (Game/GamePrefab.cpp). */
        default:
            buildGamePrefab(rEntity, shaderId_, rPrefab, rParams_, inScene);
            break;
        }
    }
    catch(const std::runtime_error& rError)
    {
        std::fprintf(stderr, "Spawn: skipped %s %s: %s\n", rPrefab.name.c_str(), rParams_.what.c_str(), rError.what());
        pPool_->remove(id);
        if(!inScene) Globals::Game::spatialIndex.dirty = indexWasDirty;
        return UninitializedPoolId;
    }
    /* The editor's prefab-wide behaviour, bound area, texture and model edits go on top of what the prefab's script setup gave. */
    if(inScene)
    {
        applyPrefabBehaviors(prefabId_, id);
        applyPrefabShapes(prefabId_, id);
        if(rPrefab.kind != PrefabKind::External)
        {
            applyPrefabMesh(prefabId_, id);
            applyPrefabTexture(prefabId_, id);
        }
    }
    if(!inScene) Globals::Game::spatialIndex.dirty = indexWasDirty;
    return id;
}
