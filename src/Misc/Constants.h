#pragma once 
#include <cstdint>
#include "Paths.h"
#include <string>
#include "PoolId.h"

inline std::string shaderFolderPath = Paths::Shaders; 
inline std::string textureFolderPath = Paths::ExternalTextures;
inline std::string modelFolderPath = Paths::ExternalModels;
inline const uint32_t UninitializedId = std::numeric_limits<uint32_t>::max();  
inline const PoolId UninitializedPoolId = PoolId{UninitializedId, UninitializedId};