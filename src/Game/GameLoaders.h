#pragma once
#include "ResourceLoader.h"
#include "PoolId.h"
#include "GameKeys.h"
#include "GLTFKeys.h"
#include "CollisionMesh.h"
#include <string>
#include <vector>

/* Loading of the resources the game's instances use (Game/GameLoaders.cpp): the SADX and Shadow formats. */
namespace Loading
{
    PoolId addSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_);
    PoolId addSALevelInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_, const std::vector<SATextureReplacement>& rReplacedTextures_ = {});
    PoolId addONELevelInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_);
    PoolId addONEInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey* pTextures_);
    PoolId addSACollision(const SALandTableKey& rLandTable_);
    PoolId addSAShapes(const SACollisionShapesKey& rShapes_);
    PoolId addSAShapes(const std::vector<SACollisionShapesKey>& rShapes_);
    PoolId addONEShapes(const STH2005::CollisionKey& rShapes_);
    PoolId addONEShapes(const std::vector<STH2005::CollisionKey>& rShapes_);
    PoolId addONECollision(const STH2005::LevelKey& rLevel_);
    void releaseSAMesh(PoolId saMeshId_);
    void releaseTextureSet(PoolId textureSetId_);
    void releaseONE(PoolId archiveId_);
    void playSAMotion(PoolId saInstanceId_, const SAMotionKey& rKey_);
    void changeSAMesh(PoolId saInstanceId_, const SAModelKey& rMesh_);
    void changeSATextureSet(PoolId saInstanceId_, const SATextureSetKey* pTextures_);
    void releaseSAMotion(PoolId saMotionId_);
}
