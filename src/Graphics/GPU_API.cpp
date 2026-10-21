#include "GPU_API.h"
#include "GPU_Globals.h"
#include "Texture.h"
#include <format>
#include <stdexcept>

namespace Graphics 
{
    PoolId addBasicMesh(const char* name_)
    {
        return basicMeshes.add(name_); 
    } 

    PoolId addTexture(const char* name_, uint32_t width_, uint32_t height_, uint32_t channels_)
    {
        return textures.add(name_, width_, height_, channels_); 
    }

    PoolId addShader(const char* name_)
    {
        return shaders.add(name_, name_);
    }

    BasicMesh& getBasicMesh(PoolId id_)
    {
        return basicMeshes[id_]; 
    }

    Texture& getTexture(PoolId id_)
    {
        return textures[id_]; 
    } 

    Shader& getShader(PoolId id_)
    {
        return shaders[id_];
    }

    PoolId getBasicMeshId(const char* name_)
    {
        return basicMeshes.getIdByName(name_); 
    }

    PoolId getTextureId(const char* name_)
    {
        return textures.getIdByName(name_);
    }

    PoolId getShaderId(const char* name_)
    {
        return shaders.getIdByName(name_);
    }

    BasicMesh& getBasicMeshByName(const char* name_)
    {
        return basicMeshes[basicMeshes.getIdByName(name_)];
    }

    Texture& getTextureByName(const char* name_)
    {
        return textures[textures.getIdByName(name_)];
    }

    Shader& getShaderByName(const char* name_)
    {
        return shaders[shaders.getIdByName(name_)];
    }

}