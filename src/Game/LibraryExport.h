#pragma once
#include <cstddef>
#include <string>

/*
Library.gltf (docs/TODO.md, "HubWorld.gltf authoring pipeline", step 3): every textured model of the entity tables of both games in one glTF, for the author to copy parts from. EDITOR only.
*/
namespace Loading
{
struct LibraryReport
{
	size_t written = 0;
	size_t levels = 0;
	size_t failed = 0;
	size_t duplicates = 0;
};

/*
Writes Library.gltf, Library.bin and the textures (PNG, in the folder next to it) into rFolder_ (made if missing). One root node per model, named "<game>/<model>", with its parts below it as nodes (SADX: the model's NJS_OBJECT nodes; Shadow: the DFF's frames); a part's
primitives are the model's submeshes, in the importers' own numbers (no conversion).
A model is a model of an entity key (forEachSAEntityKey / forEachONEEntityKey) with the textures that key names, so its textures are known; the same model and textures reached twice is written once. Level geometry is in too: every level file of Assets/Levels of the game's engines ("sadx": every visible land table piece; "sth2005": every sector), as root nodes "<game>/Level/<name>" ("#pN" for further parts: a model number holds 4096 primitives), with the level's textures; skippedTextures / skippedEntries are not applied.
Every vertex carries the writer's source fields: SRCFLAG = game (1 SADX, 2 Shadow), SRC = model number * 4096 + the primitive's number in the model, SRCIDX = BasicMesh::sourceVertex, ORIG = its position. The model number is the model's
place in the walk (SADX keys, then Shadow keys), so it changes when the generated tables do: the node's extras (game, rel/archive, offset/name, texture, model number) are what names the model, SRC only pairs a vertex with its model inside one Library.gltf.
A model that fails to load or write is reported on stderr and skipped. Throws std::runtime_error if the folder or the glTF can't be written, or the model count (levels' parts included) passes what SRC holds (4096 models).
*/
LibraryReport exportLibrary(const std::string& rFolder_);
}
