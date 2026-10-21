#include "GPU_API.h"
#include "GPU_Globals.h"
#include "SAInstance.h"
#include <format>

namespace Graphics 
{
    PoolId addSAMesh(const char* name_)
    {
        return Ninja::importSAMdl(name_, std::format("{}{}.sa1mdl", modelFolderPath, name_).c_str()); 
    }

    PoolId addTextureSet(const char* name_)
    {
        return textureSets.add(name_);
    }

    SAInstance& getSAInstance(PoolId id_)
    {
        return saInstances[id_];
    } 

    ONEInstance& getONEInstance(PoolId id_)
    {
        return oneInstances[id_];
    }

    SALevelInstance& getSALevelInstance(PoolId id_)
    {
        return saLevelInstances[id_];
    }

    ONELevelInstance& getONELevelInstance(PoolId id_)
    {
        return oneLevelInstances[id_];
    }

    TextureSet& getTextureSet(PoolId id_)
    {
        return textureSets[id_];
    }

    SAMesh& getSAMesh(PoolId id_)
    {
        return saMeshes[id_];
    }

    PoolId getTextureSetId(const char* name_)
    {
        return textureSets.getIdByName(name_);
    }

    PoolId getSAMeshId(const char* name_)
    {
        return saMeshes.getIdByName(name_);
    }

    SAMesh& getSAMeshByName(const char* name_)
    {
        return saMeshes[saMeshes.getIdByName(name_)];
    }
}
