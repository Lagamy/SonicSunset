#include "PrefabThumbnails.h"
#include "API.h"
#include "Globals.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include <cstdio>
#include <exception>
#include <string>

/* The part of PrefabThumbnails that is the game's (the engine side is EngineEditor/Panels/PrefabThumbnails.cpp): the pictures of the disc models of the Assets tab. */
namespace Graphics
{
/*
A model of a disc as a picture, with no prefab: source_ is a catalog source of the AssetPanel. SADX "<rel> @ 0x<offset>" is the Ninja model whose root NJS_OBJECT is at that offset of the .rel (importSAModel), drawn untextured (which texture archive goes with a
model is not known); Shadow "<archive> > <entry>.DFF" is a static model (a part with no .BON, see importModel), drawn with the .TXD of the same name in the same archive if textured_ (the caller found the entry there; hypothesis: that is how the stage objects are paired) and untextured if not.
The entity is made in the pool of its own and removed after the draw, like a prefab's. Returns 0 if it can't be drawn (a land table, which is not a model, another file type, or an importer throwing, which is reported on stderr).
*/
GLuint PrefabThumbnails::renderModel(int game_, const std::string& source_, bool textured_)
{
	PoolId entityId = UninitializedPoolId;
	try
	{
		PoolId shaderId = getShaderId(gameShaderName(GameSources[game_]));
		entityId = addEntity("(thumbnail)", glm::vec3(0.0f), &this->entities);
		if(game_ == 0)
		{
			size_t split = source_.find(" @ 0x");
			if(split == std::string::npos || source_.find("land table", split) != std::string::npos)
			{
				this->entities.remove(entityId);
				return 0;
			}
			SAModelKey key{this->keep(source_.substr(0, split)), static_cast<uint32_t>(std::stoul(source_.substr(split + 5), nullptr, 16))};
			this->entities[entityId].setSAGPUInstance(shaderId, key);
		}
		else
		{
			size_t split = source_.find(" > ");
			if(split == std::string::npos)
			{
				this->entities.remove(entityId);
				return 0;
			}
			const char* pArchive = this->keep(source_.substr(0, split));
			std::string entry = source_.substr(split + 3);
			STH2005::ModelKey key{{pArchive}, this->keep("(thumbnail)/" + source_), {{this->keep(entry), nullptr, nullptr}}};
			std::string textureEntry = entry.substr(0, entry.rfind('.')) + ".TXD";
			if(textured_) this->entities[entityId].setONEGPUInstance(shaderId, key, STH2005::TextureKey{{pArchive}, this->keep(textureEntry)});
			else this->entities[entityId].setONEGPUInstance(shaderId, key);
		}
	}
	catch(const std::exception& rError_)
	{
		std::fprintf(stderr, "PrefabThumbnails: %s: %s\n", source_.c_str(), rError_.what());
		if(entityId != UninitializedPoolId) this->entities.remove(entityId);
		return 0;
	}
	return this->draw(entityId, CubeGrey);
}
}
