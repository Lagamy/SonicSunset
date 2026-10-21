#include "ResourceLoader.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include "CollisionFile.h"
#include "ModelFile.h"
#include "TextureFile.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <map>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/*
Loading and lifetime of the resources instances use: the glTF levels (reads files, runs the importers, keeps what the pools store). The resources of the game's own formats are in Game/GameLoaders.cpp. The Graphics pools themselves only store.
*/
namespace Loading
{
    using namespace Graphics;

    /*
    The instance of a glTF level, read from its folder (GLTFKeys.h): every <levelName>_*.chunk.model.json of Models/ (written by the editor's import, Loading/GLTFImport.h) is one chunk, each of its submeshes one GLTFLevelSubmesh with its bounding
    sphere; the PNGs the chunks name (also in Models/) are added to Graphics::textures as "<levelName> <file name>" (uploaded now, removed by GLTFLevelInstance::destroy). The GPU buffers of a submesh are made by the streaming
    when it comes near. Reads files only (no scan of the external assets), so it works in a build without EDITOR. Throws if a file can't be read or there is no chunk, after removing what was made.
    */
    PoolId addGLTFLevelInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
    {
        namespace fs = std::filesystem;
        fs::path models = fs::path(rLevel_.folder()) / "Models";
        std::vector<fs::path> chunkFiles;
        const std::string prefix = rLevel_.levelName + "_";
        if(fs::is_directory(models))
        {
            for(const fs::directory_entry& rEntry : fs::directory_iterator(models))
            {
                std::string name = rEntry.path().filename().string();
                if(rEntry.is_regular_file() && name.starts_with(prefix) && name.ends_with(".chunk.model.json")) chunkFiles.push_back(rEntry.path());
            }
        }
        std::sort(chunkFiles.begin(), chunkFiles.end());
        if(chunkFiles.empty()) throw std::runtime_error(std::format("glTF level '{}': no chunk files ({}_*.chunk.model.json) in {}; import a glTF model first.", rLevel_.levelName, rLevel_.levelName, models.string()));

        GLTFLevelInstance level;
        level.shaderId = shaderId_;
        try
        {
            std::map<std::string, PoolId> textureOfName;
            for(const fs::path& rFile : chunkFiles)
            {
                External::ModelData model = External::readModel(rFile.string());
                if(model.layout.key() != VertexLayout::standard().key()) throw std::runtime_error(std::format("{}: the vertex layout is not the standard one.", rFile.string()));
                for(External::ModelSubmesh& rSource : model.submeshes)
                {
                    if(rSource.vertexCount == 0) continue;
                    GLTFLevelSubmesh submesh{};
                    submesh.textureId = UninitializedPoolId;
                    if(!rSource.texture.empty())
                    {
                        auto found = textureOfName.find(rSource.texture);
                        if(found == textureOfName.end())
                        {
                            PoolId id = textures.add(std::format("{} {}", rLevel_.levelName, rSource.texture).c_str(), External::readTexture((models / (rSource.texture + ".png")).string()));
                            level.textureIds.push_back(id);
                            textures[id].create();
                            found = textureOfName.emplace(rSource.texture, id).first;
                        }
                        submesh.textureId = found->second;
                    }
                    submesh.mesh.verticies.bytes = std::move(rSource.vertices);
                    submesh.mesh.indicies = std::move(rSource.indices);
                    const Graphics::Vertex* pVertices = submesh.mesh.verticies.begin();
                    size_t count = submesh.mesh.verticies.size();
                    glm::vec3 minimum = pVertices[0].position, maximum = pVertices[0].position;
                    for(size_t i = 0; i < count; i++)
                    {
                        minimum = glm::min(minimum, pVertices[i].position);
                        maximum = glm::max(maximum, pVertices[i].position);
                    }
                    submesh.center = (minimum + maximum) * 0.5f;
                    submesh.radius = glm::distance(minimum, maximum) * 0.5f;
                    level.submeshes.push_back(std::move(submesh));
                }
            }
        }
        catch(...)
        {
            level.destroy();
            throw;
        }
        return gltfLevelInstances.add(std::move(level));
    }

    /* The collision of a glTF level: the triangles of the collision files the key names (Collision/<name>.collision.json, CollisionFile.h) in one Collision, every piece solid (flags 0 is not listed in the SADXLevel remap, whose fallback is Solid). */
    PoolId addGLTFCollision(const GLTFLevelKey& rLevel_)
    {
        Physics::Collision collision;
        for(const std::string& rName : rLevel_.collisions) collision.mesh.append(readGLTFCollisionFile(rLevel_, rName));
        collision.computeBounds();
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapSADXLevel);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /* Adds one collision file to a glTF level's live collision: the triangles are appended, the bounds are made again (the collision keeps its position) and the new pieces' flags are given their behaviours. The caller rebuilds the quad tree. */
    void appendGLTFCollision(PoolId collisionId_, const GLTFLevelKey& rLevel_, const std::string& rName_)
    {
        Physics::Collision& rCollision = Globals::Physics::collisions[collisionId_];
        rCollision.mesh.append(readGLTFCollisionFile(rLevel_, rName_));
        glm::vec3 position = rCollision.position;
        rCollision.position = glm::vec3(0.0f);
        rCollision.computeBounds();
        rCollision.setPosition(position);
        Globals::Game::baBehaviors.resolve(rCollision, Globals::Game::baBehaviors.remapSADXLevel);
    }

    /* One collision file of a glTF level. */
    Physics::CollisionMesh readGLTFCollisionFile(const GLTFLevelKey& rLevel_, const std::string& rName_)
    {
        return External::readCollision(rLevel_.folder() + "/Collision/" + rName_ + ".collision.json");
    }
}
