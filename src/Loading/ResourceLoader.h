#pragma once
#include "PoolId.h"
#include "GLTFKeys.h"
#include "CollisionMesh.h"
#include <string>
#include <vector>

/* Loading of the resources the engine's instances use (Loading/ResourceLoader.cpp): glTF levels. The ones of the game's own formats are in Game/GameLoaders.h. */
namespace Loading
{
    PoolId addGLTFLevelInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_);
    PoolId addGLTFCollision(const GLTFLevelKey& rLevel_);
    void appendGLTFCollision(PoolId collisionId_, const GLTFLevelKey& rLevel_, const std::string& rName_);
    Physics::CollisionMesh readGLTFCollisionFile(const GLTFLevelKey& rLevel_, const std::string& rName_);
}
