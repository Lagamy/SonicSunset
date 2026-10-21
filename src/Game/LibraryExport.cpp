#include "LibraryExport.h"
#include "GameEntity.h"
#include "GameGraphicsGlobals.h"
#include "GameLevelLoader.h"
#include "GameLoaders.h"
#include "GLTFFile.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "Paths.h"
#include "TextureFile.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Loading
{
namespace
{
constexpr uint32_t PiecesPerModel = 4096;
constexpr uint32_t ModelsPerLibrary = (1u << 24) / PiecesPerModel;
constexpr uint32_t FlagSADX = 1;
constexpr uint32_t FlagShadow = 2;

/* One model to write: where it comes from and the textures its key names. The keys live for the whole run (forEachSAEntityKey / forEachONEEntityKey). */
struct LibraryModel
{
	uint32_t flag;
	std::string name;
	SAModelKey saModel{nullptr, 0};
	const char* saTextures = nullptr;
	const STH2005::ModelKey* pOneModel = nullptr;
	const STH2005::TextureKey* pOneTextures = nullptr;
};

/* A file-name-safe version of a name. */
std::string safeName(const std::string& rName_)
{
	std::string name = rName_;
	for(char& rChar : name)
	{
		bool plain = (rChar >= '0' && rChar <= '9') || (rChar >= 'a' && rChar <= 'z') || (rChar >= 'A' && rChar <= 'Z') || rChar == '_' || rChar == '-';
		if(!plain) rChar = '_';
	}
	return name;
}

/* Translation, rotation and scale of a transform with no shear (what the importers' local transforms are); a mirroring scale is not told apart from a rotation. */
void decomposeTransform(const glm::mat4& rMatrix_, External::GLTFNode& rNode_)
{
	rNode_.translation = glm::vec3(rMatrix_[3]);
	glm::vec3 axes[3] = {glm::vec3(rMatrix_[0]), glm::vec3(rMatrix_[1]), glm::vec3(rMatrix_[2])};
	glm::vec3 scale(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
	for(int i = 0; i < 3; i++)
	{
		if(scale[i] > 0.0f) axes[i] /= scale[i];
	}
	rNode_.scale = scale;
	rNode_.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
}

struct LibraryBuilder
{
	External::GLTFScene scene;
	std::filesystem::path folder;
	std::map<std::string, int> materialOfImage;
	uint32_t nextModel = 0;

	/* The next model number; a level takes one for each part of PiecesPerModel primitives. Throws past what SRC holds. */
	uint32_t takeModelNumber()
	{
		if(this->nextModel >= ModelsPerLibrary) throw std::runtime_error(std::format("Library: more than {} models, SRC holds no more", ModelsPerLibrary));
		return this->nextModel++;
	}

	/*
	The material of a texture, its PNG written on the first use: rName_ is the file name without extension. A Texture frees its pixels once uploaded (Texture::create), so they are read back from the GL texture's first level (the stored RGBA8, as uploaded: SRGB8_ALPHA8 reads back
	undecoded); transparent texels have the color Texture::create bled into them.
	*/
	int materialFor(const std::string& rName_, const Graphics::Texture& rTexture_)
	{
		auto found = this->materialOfImage.find(rName_);
		if(found != this->materialOfImage.end()) return found->second;
		std::string uri = rName_ + ".png";
		Graphics::Texture image(rTexture_.width, rTexture_.height, 4);
		image.filter = rTexture_.filter;
		image.wrap = rTexture_.wrap;
		image.mipmaps = rTexture_.mipmaps;
		glBindTexture(GL_TEXTURE_2D, rTexture_.textureId);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
		External::writeTexture((this->folder / uri).string(), image);
		External::GLTFMaterial material;
		material.name = rName_;
		material.image = static_cast<int>(this->scene.images.size());
		this->scene.images.push_back(uri);
		this->scene.materials.push_back(material);
		int index = static_cast<int>(this->scene.materials.size()) - 1;
		this->materialOfImage[rName_] = index;
		return index;
	}

	int addNode(const std::string& rName_)
	{
		External::GLTFNode node;
		node.name = rName_;
		this->scene.nodes.push_back(node);
		return static_cast<int>(this->scene.nodes.size()) - 1;
	}
};

/*
One submesh as a primitive. The vertex's source fields are what the HubWorld.gltf pipeline reads back (TODO.md): SRC the piece (model number and the primitive's number in the model), SRCFLAG the game, SRCIDX the vertex's index in the source file's
positions (BasicMesh::sourceVertex), ORIG its position as exported.
*/
External::GLTFPrimitive makePrimitive(const Graphics::BasicMesh& rMesh_, uint32_t modelNumber_, uint32_t piece_, uint32_t flag_, int material_)
{
	size_t count = rMesh_.verticies.size();
	if(rMesh_.sourceVertex.size() != count) throw std::runtime_error(std::format("piece {} has {} source vertices for {} vertices", piece_, rMesh_.sourceVertex.size(), count));
	if(piece_ >= PiecesPerModel) throw std::runtime_error(std::format("the model has more than {} pieces", PiecesPerModel));
	External::GLTFPrimitive primitive;
	for(const Graphics::Vertex& rVertex : rMesh_.verticies)
	{
		primitive.positions.push_back(rVertex.position);
		primitive.normals.push_back(rVertex.normal);
		primitive.uvs.push_back(rVertex.uv);
		primitive.colors.push_back(rVertex.color);
	}
	primitive.indices = rMesh_.indicies;
	if(primitive.indices.empty())
	{
		primitive.indices.resize(count);
		std::iota(primitive.indices.begin(), primitive.indices.end(), 0u);
	}
	primitive.material = material_;
	primitive.src.assign(count, modelNumber_ * PiecesPerModel + piece_);
	primitive.srcFlag.assign(count, flag_);
	primitive.srcIdx = rMesh_.sourceVertex;
	primitive.orig = primitive.positions;
	return primitive;
}

void addModelExtras(External::GLTFNode& rRoot_, const LibraryModel& rModel_, uint32_t modelNumber_)
{
	rRoot_.extras.emplace_back("game", rModel_.flag == FlagSADX ? "SADX" : "Shadow");
	rRoot_.extras.emplace_back("model", std::to_string(modelNumber_));
	if(rModel_.flag == FlagSADX)
	{
		rRoot_.extras.emplace_back("rel", rModel_.saModel.rel);
		rRoot_.extras.emplace_back("offset", std::format("0x{:X}", rModel_.saModel.rootOffset));
		rRoot_.extras.emplace_back("textures", rModel_.saTextures != nullptr ? rModel_.saTextures : "");
	}
	else
	{
		rRoot_.extras.emplace_back("archive", rModel_.pOneModel->archive.path);
		rRoot_.extras.emplace_back("entry", rModel_.pOneModel->name);
		rRoot_.extras.emplace_back("textures", rModel_.pOneTextures != nullptr ? rModel_.pOneTextures->entry : "");
	}
}

/* The name a texture has in its set (the texture of a submesh is found by its pool id, which is the same for a model's texture index and a land table's texlist index); empty if the set doesn't have it. */
std::string textureNameInSet(const Graphics::TextureSet& rSet_, PoolId textureId_)
{
	for(size_t i = 0; i < rSet_.ordered.size() && i < rSet_.names.size(); i++)
	{
		if(rSet_.ordered[i] == textureId_) return rSet_.names[i];
	}
	return "";
}

/* The number of pieces (submeshes, empty ones too) an SAInstance's mesh has. */
uint32_t countSAPieces(PoolId instanceId_)
{
	uint32_t count = 0;
	for(const Graphics::SANode& rNode : Graphics::saMeshes[Graphics::saInstances[instanceId_].saMeshId].nodes) count += static_cast<uint32_t>(rNode.submeshes.size());
	return count;
}

/*
The NJS nodes of an SAInstance's mesh as nodes under rParent_, with their meshes; its textures are the instance's binding. Pieces are numbered in node and submesh order, from rPiece_ (advanced). rSetName_ is the texture set's file name, the front of the material names.
*/
void addSANodes(LibraryBuilder& rBuilder_, PoolId instanceId_, int parent_, const std::string& rName_, uint32_t modelNumber_, uint32_t& rPiece_, const std::string& rSetName_)
{
	const Graphics::SAInstance& rInstance = Graphics::saInstances[instanceId_];
	const Graphics::SAMesh& rMesh = Graphics::saMeshes[rInstance.saMeshId];
	const Graphics::TextureSet* pSet = rInstance.textureSetId != UninitializedPoolId ? &Graphics::textureSets[rInstance.textureSetId] : nullptr;
	const Graphics::TextureBinding* pBinding = rInstance.textureBindingId != UninitializedPoolId ? &Graphics::textureBindings[rInstance.textureBindingId] : nullptr;

	std::vector<int> nodeOf(rMesh.nodes.size());
	for(size_t i = 0; i < rMesh.nodes.size(); i++)
	{
		const Graphics::SANode& rNode = rMesh.nodes[i];
		nodeOf[i] = rBuilder_.addNode(std::format("{}_n{}", rName_, i));
		decomposeTransform(rNode.localTransform, rBuilder_.scene.nodes[nodeOf[i]]);
		int parent = rNode.parent < 0 ? parent_ : nodeOf[rNode.parent];
		rBuilder_.scene.nodes[parent].children.push_back(nodeOf[i]);
		if(rNode.submeshes.empty()) continue;

		External::GLTFMesh mesh;
		mesh.name = rBuilder_.scene.nodes[nodeOf[i]].name;
		for(size_t s = 0; s < rNode.submeshes.size(); s++)
		{
			const Graphics::SASubmesh& rSubmesh = rNode.submeshes[s];
			int material = -1;
			if(pBinding != nullptr && i < pBinding->submeshTextureIds.size() && s < pBinding->submeshTextureIds[i].size())
			{
				PoolId textureId = pBinding->submeshTextureIds[i][s];
				std::string textureName = textureId != UninitializedPoolId && pSet != nullptr ? textureNameInSet(*pSet, textureId) : "";
				if(!textureName.empty()) material = rBuilder_.materialFor(std::format("{}_{}", rSetName_, safeName(textureName)), Graphics::textures[textureId]);
			}
			if(rSubmesh.mesh.verticies.size() > 0) mesh.primitives.push_back(makePrimitive(rSubmesh.mesh, modelNumber_, rPiece_, FlagSADX, material));
			rPiece_++;
		}
		if(mesh.primitives.empty()) continue;
		rBuilder_.scene.meshes.push_back(std::move(mesh));
		rBuilder_.scene.nodes[nodeOf[i]].mesh = static_cast<int>(rBuilder_.scene.meshes.size()) - 1;
	}
}

/* The SADX model made into nodes under a new root; its textures are the instance's binding (the .gvm the entity key names). */
void addSAModel(LibraryBuilder& rBuilder_, const LibraryModel& rModel_, uint32_t modelNumber_, PoolId shaderId_)
{
	SATextureSetKey textures{rModel_.saTextures};
	PoolId instanceId = addSAInstance(shaderId_, rModel_.saModel, rModel_.saTextures != nullptr ? &textures : nullptr);
	size_t nodeMark = rBuilder_.scene.nodes.size();
	size_t meshMark = rBuilder_.scene.meshes.size();
	try
	{
		std::string setName = rModel_.saTextures != nullptr ? safeName(std::filesystem::path(rModel_.saTextures).stem().string()) : "";
		int root = rBuilder_.addNode(rModel_.name);
		addModelExtras(rBuilder_.scene.nodes[root], rModel_, modelNumber_);
		uint32_t piece = 0;
		addSANodes(rBuilder_, instanceId, root, rModel_.name, modelNumber_, piece, setName);
		rBuilder_.scene.rootNodes.push_back(root);
	}
	catch(...)
	{
		rBuilder_.scene.nodes.resize(nodeMark);
		rBuilder_.scene.meshes.resize(meshMark);
		Graphics::saInstances.remove(instanceId);
		throw;
	}
	Graphics::saInstances.remove(instanceId);
}

/* The Shadow model made into nodes under a new root, one per DFF frame; an atomic's submeshes go on its frame's node. Its textures are the .TXD entry of the entity key (the binding of the instance). */
void addONEModel(LibraryBuilder& rBuilder_, const LibraryModel& rModel_, uint32_t modelNumber_, PoolId shaderId_)
{
	PoolId instanceId = addONEInstance(shaderId_, *rModel_.pOneModel, rModel_.pOneTextures);
	size_t nodeMark = rBuilder_.scene.nodes.size();
	size_t meshMark = rBuilder_.scene.meshes.size();
	try
	{
		const Graphics::ONEInstance& rInstance = Graphics::oneInstances[instanceId];
		const STH2005::ONE& rArchive = Graphics::oneArchives[rInstance.archiveId];
		const Graphics::RenderWareMesh& rMesh = rArchive.meshes[rInstance.meshIndex];
		const Graphics::TextureBinding* pBinding = rInstance.bindingIndex != STH2005::NoIndex ? &rArchive.bindings[rInstance.bindingIndex] : nullptr;
		std::string archiveName = safeName(std::filesystem::path(rModel_.pOneModel->archive.path).stem().string());

		int root = rBuilder_.addNode(rModel_.name);
		addModelExtras(rBuilder_.scene.nodes[root], rModel_, modelNumber_);
		std::vector<int> nodeOf(rMesh.frames.size());
		for(size_t i = 0; i < rMesh.frames.size(); i++)
		{
			const Graphics::RenderWareFrame& rFrame = rMesh.frames[i];
			nodeOf[i] = rBuilder_.addNode(rFrame.name.empty() ? std::format("{}_f{}", rModel_.name, i) : std::format("{}_{}", rModel_.name, rFrame.name));
			decomposeTransform(rFrame.localTransform, rBuilder_.scene.nodes[nodeOf[i]]);
			int parent = rFrame.parent < 0 ? root : nodeOf[rFrame.parent];
			rBuilder_.scene.nodes[parent].children.push_back(nodeOf[i]);
		}
		uint32_t piece = 0;
		for(size_t a = 0; a < rMesh.atomics.size(); a++)
		{
			const Graphics::RenderWareAtomic& rAtomic = rMesh.atomics[a];
			External::GLTFMesh mesh;
			mesh.name = std::format("{}_a{}", rModel_.name, a);
			for(size_t s = 0; s < rAtomic.submeshes.size(); s++)
			{
				const Graphics::RenderWareSubmesh& rSubmesh = rAtomic.submeshes[s];
				int material = -1;
				if(!rSubmesh.textureName.empty() && pBinding != nullptr && a < pBinding->submeshTextureIds.size() && s < pBinding->submeshTextureIds[a].size())
				{
					PoolId textureId = pBinding->submeshTextureIds[a][s];
					if(textureId != UninitializedPoolId) material = rBuilder_.materialFor(std::format("{}_{}", archiveName, safeName(rSubmesh.textureName)), Graphics::textures[textureId]);
				}
				if(rSubmesh.mesh.verticies.size() > 0) mesh.primitives.push_back(makePrimitive(rSubmesh.mesh, modelNumber_, piece, FlagShadow, material));
				piece++;
			}
			if(mesh.primitives.empty() || rAtomic.frame < 0) continue;
			External::GLTFNode& rNode = rBuilder_.scene.nodes[nodeOf[rAtomic.frame]];
			if(rNode.mesh < 0)
			{
				rBuilder_.scene.meshes.push_back(std::move(mesh));
				rNode.mesh = static_cast<int>(rBuilder_.scene.meshes.size()) - 1;
			}
			else
			{
				External::GLTFMesh& rOwn = rBuilder_.scene.meshes[rNode.mesh];
				for(External::GLTFPrimitive& rPrimitive : mesh.primitives) rOwn.primitives.push_back(std::move(rPrimitive));
			}
		}
		rBuilder_.scene.rootNodes.push_back(root);
	}
	catch(...)
	{
		rBuilder_.scene.nodes.resize(nodeMark);
		rBuilder_.scene.meshes.resize(meshMark);
		Graphics::oneInstances.remove(instanceId);
		throw;
	}
	Graphics::oneInstances.remove(instanceId);
}

/*
The parts a level is written in: SRC holds PiecesPerModel primitives per model number, a level has more, so it is cut into parts, each a root node of its own with its own model number. A part's extras name the level and its number.
*/
struct LevelParts
{
	LibraryBuilder& rBuilder;
	std::string name;
	std::vector<std::pair<std::string, std::string>> identity;
	int root = -1;
	uint32_t number = 0;
	uint32_t piece = 0;
	size_t count = 0;

	void start()
	{
		this->number = this->rBuilder.takeModelNumber();
		this->piece = 0;
		this->root = this->rBuilder.addNode(this->count == 0 ? this->name : std::format("{}#p{}", this->name, this->count));
		External::GLTFNode& rRoot = this->rBuilder.scene.nodes[this->root];
		rRoot.extras.emplace_back("model", std::to_string(this->number));
		rRoot.extras.emplace_back("level", this->name);
		rRoot.extras.emplace_back("part", std::to_string(this->count));
		for(const auto& rField : this->identity) rRoot.extras.push_back(rField);
		this->rBuilder.scene.rootNodes.push_back(this->root);
		this->count++;
	}

	/* Room for pieceCount_ more primitives in the part, or a new part. */
	void reserve(uint32_t pieceCount_)
	{
		if(this->root < 0 || (this->piece > 0 && this->piece + pieceCount_ > PiecesPerModel)) this->start();
	}
};

/* The SADX level made into parts: each visible COL piece (addSALevelInstance) a node tree under its part's root, with the textures of the level's .gvm. Nothing is skipped (the level file's skippedTextures is not applied). */
void addSALevel(LibraryBuilder& rBuilder_, const std::string& rName_, const Recipe& rRecipe_, PoolId shaderId_)
{
	static const std::vector<std::string> noSkipped;
	PoolId levelId = addSALevelInstance(shaderId_, *rRecipe_.saLandTable, *rRecipe_.saTextures, noSkipped);
	size_t nodeMark = rBuilder_.scene.nodes.size();
	size_t meshMark = rBuilder_.scene.meshes.size();
	size_t rootMark = rBuilder_.scene.rootNodes.size();
	try
	{
		LevelParts parts{rBuilder_, rName_, {}};
		parts.identity.emplace_back("game", "SADX");
		parts.identity.emplace_back("rel", rRecipe_.saLandTable->rel);
		parts.identity.emplace_back("offset", std::format("0x{:X}", rRecipe_.saLandTable->offset));
		parts.identity.emplace_back("textures", rRecipe_.saTextures->path);
		std::string setName = safeName(std::filesystem::path(rRecipe_.saTextures->path).stem().string());
		const Graphics::SALevelInstance& rLevel = Graphics::saLevelInstances[levelId];
		for(size_t k = 0; k < rLevel.pieces.size(); k++)
		{
			PoolId instanceId = rLevel.pieces[k].saInstanceId;
			parts.reserve(countSAPieces(instanceId));
			addSANodes(rBuilder_, instanceId, parts.root, std::format("{}_c{}", rName_, k), parts.number, parts.piece, setName);
		}
	}
	catch(...)
	{
		rBuilder_.scene.nodes.resize(nodeMark);
		rBuilder_.scene.meshes.resize(meshMark);
		rBuilder_.scene.rootNodes.resize(rootMark);
		Graphics::saLevelInstances.remove(levelId);
		throw;
	}
	Graphics::saLevelInstances.remove(levelId);
}

/* The Shadow stage made into parts: each sector (addONELevelInstance) a node under its part's root, its submeshes the primitives, textures by name from the stage's texture archive. Nothing is skipped (skippedEntries is not applied). */
void addONELevel(LibraryBuilder& rBuilder_, const std::string& rName_, const Recipe& rRecipe_, PoolId shaderId_)
{
	static const std::vector<std::string> noSkipped;
	PoolId levelId = addONELevelInstance(shaderId_, *rRecipe_.oneLevel, noSkipped);
	size_t nodeMark = rBuilder_.scene.nodes.size();
	size_t meshMark = rBuilder_.scene.meshes.size();
	size_t rootMark = rBuilder_.scene.rootNodes.size();
	try
	{
		const STH2005::LevelKey& rKey = *rRecipe_.oneLevel;
		LevelParts parts{rBuilder_, rName_, {}};
		parts.identity.emplace_back("game", "Shadow");
		parts.identity.emplace_back("archive", rKey.textures.archive.path);
		parts.identity.emplace_back("textures", rKey.textures.entry);
		std::string prefix = safeName(std::filesystem::path(rKey.textures.archive.path).stem().string()) + "_" + safeName(rKey.textures.entry);
		const Graphics::ONELevelInstance& rLevel = Graphics::oneLevelInstances[levelId];
		std::map<uint32_t, std::string> textureNames;
		for(const Renderware::TxdTextures& rDictionary : Graphics::oneArchives[rLevel.archiveId].textureDictionaries)
		{
			for(const auto& rEntry : rDictionary.byName) textureNames.emplace(rEntry.second.id, rEntry.first);
		}
		for(size_t k = 0; k < rLevel.pieces.size(); k++)
		{
			const Graphics::ONELevelPiece& rPiece = rLevel.pieces[k];
			parts.reserve(static_cast<uint32_t>(rPiece.submeshes.size()));
			External::GLTFMesh mesh;
			mesh.name = std::format("{}_s{}", rName_, k);
			for(const Graphics::ONELevelSubmesh& rSubmesh : rPiece.submeshes)
			{
				int material = -1;
				auto found = rSubmesh.textureId != UninitializedPoolId ? textureNames.find(rSubmesh.textureId.id) : textureNames.end();
				if(found != textureNames.end()) material = rBuilder_.materialFor(std::format("{}_{}", prefix, safeName(found->second)), Graphics::textures[rSubmesh.textureId]);
				if(rSubmesh.mesh.verticies.size() > 0) mesh.primitives.push_back(makePrimitive(rSubmesh.mesh, parts.number, parts.piece, FlagShadow, material));
				parts.piece++;
			}
			if(mesh.primitives.empty()) continue;
			int node = rBuilder_.addNode(mesh.name);
			rBuilder_.scene.nodes[parts.root].children.push_back(node);
			rBuilder_.scene.meshes.push_back(std::move(mesh));
			rBuilder_.scene.nodes[node].mesh = static_cast<int>(rBuilder_.scene.meshes.size()) - 1;
		}
	}
	catch(...)
	{
		rBuilder_.scene.nodes.resize(nodeMark);
		rBuilder_.scene.meshes.resize(meshMark);
		rBuilder_.scene.rootNodes.resize(rootMark);
		Graphics::oneLevelInstances.remove(levelId);
		throw;
	}
	Graphics::oneLevelInstances.remove(levelId);
}
}

LibraryReport exportLibrary(const std::string& rFolder_)
{
	LibraryReport report;
	std::vector<LibraryModel> models;
	std::set<std::string> seen;

	/* SADX: the entity's model and its variants with the .gvm the key names; a null rel is a variant that is not drawn. */
	forEachSAEntityKey([&](const SAEntityKey& rKey_)
	{
		auto add = [&](const SAModelKey& rModel_, const std::string& rName_)
		{
			if(rModel_.rel == nullptr) return;
			std::string id = std::format("{}@0x{:X}|{}", rModel_.rel, rModel_.rootOffset, rKey_.textures.path != nullptr ? rKey_.textures.path : "");
			if(!seen.insert(id).second)
			{
				report.duplicates++;
				return;
			}
			LibraryModel model;
			model.flag = FlagSADX;
			model.name = "SADX/" + rName_;
			model.saModel = rModel_;
			model.saTextures = rKey_.textures.path;
			models.push_back(model);
		};
		add(rKey_.model, rKey_.name);
		for(size_t i = 0; i < rKey_.variantModels.size(); i++) add(rKey_.variantModels[i], std::format("{}#v{}", rKey_.name, i));
	});

	/* Shadow: each model of the key with the key's .TXD (entry nullptr = untextured). */
	forEachONEEntityKey([&](const STH2005::EntityKey& rKey_)
	{
		for(size_t i = 0; i < rKey_.models.size(); i++)
		{
			const STH2005::ModelKey& rModel = rKey_.models[i];
			std::string id = std::format("{}|{}|{}", rModel.archive.path, rModel.name, rKey_.textures.entry != nullptr ? rKey_.textures.entry : "");
			if(!seen.insert(id).second)
			{
				report.duplicates++;
				continue;
			}
			LibraryModel model;
			model.flag = FlagShadow;
			model.name = "Shadow/" + (i == 0 ? std::string(rKey_.name) : std::format("{}#m{}", rKey_.name, i));
			model.pOneModel = &rModel;
			model.pOneTextures = rKey_.textures.entry != nullptr ? &rKey_.textures : nullptr;
			models.push_back(model);
		}
	});
	if(models.size() > ModelsPerLibrary) throw std::runtime_error(std::format("Library: {} models, SRC holds {}", models.size(), ModelsPerLibrary));

	std::filesystem::create_directories(rFolder_);
	LibraryBuilder builder;
	builder.folder = rFolder_;
	PoolId saShader = Graphics::getShaderId(gameShaderName(GameSources[0]));
	PoolId oneShader = Graphics::getShaderId(gameShaderName(GameSources[1]));
	builder.nextModel = static_cast<uint32_t>(models.size());
	for(uint32_t number = 0; number < models.size(); number++)
	{
		const LibraryModel& rModel = models[number];
		try
		{
			if(rModel.flag == FlagSADX) addSAModel(builder, rModel, number, saShader);
			else addONEModel(builder, rModel, number, oneShader);
			report.written++;
		}
		catch(const std::exception& rError_)
		{
			std::fprintf(stderr, "Library: %s: %s\n", rModel.name.c_str(), rError_.what());
			report.failed++;
		}
	}

	/* Levels: every level file of Assets/Levels whose engine is one of the game's ("gltf" levels are the user's own and not disc data); a level that fails is reported and skipped. */
	std::set<std::string> levelNames;
	if(std::filesystem::is_directory(Paths::LevelsDir))
	{
		for(const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(Paths::LevelsDir))
		{
			if(rEntry.is_directory()) levelNames.insert(rEntry.path().filename().string());
		}
	}
	for(const std::string& rLevelName : levelNames)
	{
		try
		{
			std::ifstream stream(std::string(Paths::Levels) + rLevelName + "/" + rLevelName + ".json");
			if(!stream) continue;
			nlohmann::json file = nlohmann::json::parse(stream);
			Recipe recipe;
			if(!readGameRecipe(file.at("engine").get<std::string>(), recipe, file)) continue;
			if(recipe.saLandTable != nullptr) addSALevel(builder, "SADX/Level/" + rLevelName, recipe, saShader);
			else addONELevel(builder, "Shadow/Level/" + rLevelName, recipe, oneShader);
			report.levels++;
		}
		catch(const std::exception& rError_)
		{
			std::fprintf(stderr, "Library: level %s: %s\n", rLevelName.c_str(), rError_.what());
			report.failed++;
		}
	}
	External::writeGLTF((std::filesystem::path(rFolder_) / "Library.gltf").string(), builder.scene);
	return report;
}
}
