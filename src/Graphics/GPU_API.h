#pragma once 
#include "BasicMesh.h"
#include "Texture.h"
#include "Shader.h"
#include "PoolId.h"
#include "Constants.h"
#include "GameGraphicsAPI.h"

namespace Graphics
{
    PoolId addBasicMesh(const char* name_);
    PoolId addTexture(const char* name_, uint32_t width_, uint32_t height_, uint32_t channels_ );
    PoolId addShader(const char* name_);

    BasicMesh& getBasicMesh(PoolId id_);
    Texture& getTexture(PoolId id_);
    Shader& getShader(PoolId id_);

    PoolId getBasicMeshId(const char* name_);
    PoolId getTextureId(const char* name_);
    PoolId getShaderId(const char* name_);

    BasicMesh& getBasicMeshByName(const char* name_);
    Texture& getTextureByName(const char* name_);
    Shader& getShaderByName(const char* name_);
}