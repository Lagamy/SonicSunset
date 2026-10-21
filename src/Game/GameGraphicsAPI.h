#pragma once
#include "SAInstance.h"
#include "TextureSet.h"
#include "TextureBinding.h"
#include "SAMesh.h"
#include "ONE.h"
#include "ONEInstance.h"
#include "SALevelInstance.h"
#include "ONELevelInstance.h"
#include "SAMdl.h"
#include "PoolId.h"

/* The part of the graphics API (GPU_API.h, which includes this file by name; every game supplies its own) that is the game's. Defined in Game/GameGraphicsAPI.cpp. */
namespace Graphics
{
    PoolId addSAMesh(const char* name_);
    PoolId addTextureSet(const char* name_);

    TextureSet& getTextureSet(PoolId id_);
    SAMesh& getSAMesh(PoolId id_);
    SAInstance& getSAInstance(PoolId id_); 
    ONEInstance& getONEInstance(PoolId id_);
    SALevelInstance& getSALevelInstance(PoolId id_);
    ONELevelInstance& getONELevelInstance(PoolId id_);

    PoolId getTextureSetId(const char* name_);
    PoolId getSAMeshId(const char* name_);

    SAMesh& getSAMeshByName(const char* name_);
}
