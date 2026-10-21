#include "API.h"
#include "Entity.h"
#include "Globals.h"

/*
Sets the animation an entity wants (Entity::animationIntent): the graphics side switches to it once a frame, only if it is not already playing, so a behaviour can call this every tick. The key has to outlive the
entity (the keys of ResourceKeys.h are static); the last call wins. The SA overload loops, as playSAMotion does.
*/
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.animationIntent = AnimationIntent{&rKey_, nullptr, true};
}

/* The same with the mesh the motion is for: the entity draws that mesh from then on (the motion's node count has to match it); loop_ false = the motion plays once and holds its last frame. */
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_, const SAModelKey& rMesh_, bool loop_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.animationIntent = AnimationIntent{&rKey_, nullptr, loop_, &rMesh_};
}

/* An SA entity draws this motion and mesh in place of the wanted animation while it is set (nullptr clears it); the wanted one keeps its own clock meanwhile (Entity::blinkIntent). */
void blinkAnimation(PoolId entityId_, const SAMotionKey* pKey_, const SAModelKey* pMesh_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.blinkIntent = AnimationIntent{pKey_, nullptr, true, pMesh_};
}

void changeAnimation(PoolId entityId_, const STH2005::MotionKey& rKey_, bool loop_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.animationIntent = AnimationIntent{nullptr, &rKey_, loop_};
}
